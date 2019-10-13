/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <czmq.h>
#include <inttypes.h>
#include <flux/core.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/librouter/usock.h"
#include "src/common/librouter/router.h"

enum {
    DEBUG_AUTHFAIL_ONESHOT = 1, /* force auth to fail one time */
    DEBUG_USERDB_ONESHOT = 2,   /* force userdb lookup of instance owner */
    DEBUG_OWNERDROP_ONESHOT = 4,/* drop OWNER role to USER on next connection */
};

struct connector_local {
    struct usock_server *server;
    struct router *router;
    flux_t *h;
    uid_t instance_owner;
};

/* A 'struct route_entry' is attached to the 'struct usock_conn' aux hash
 * so that when the client is destroyed, its route is also destroyed.
 * This also helps bridge uconn_recv() to router_entry_recv().
 */
static const char *route_auxkey = "flux::route";


static int client_authenticate (flux_t *h,
                                uint32_t instance_owner,
                                uid_t cuid,
                                struct auth_cred *cred)
{
    uint32_t rolemask;
    flux_future_t *f;

    if (flux_module_debug_test (h, DEBUG_AUTHFAIL_ONESHOT, true)) {
        flux_log (h, LOG_ERR, "connect by uid=%d denied by debug flag",
                  cuid);
        errno = EPERM;
        goto error;
    }
    if (!flux_module_debug_test (h, DEBUG_USERDB_ONESHOT, true)) {
        if (cuid == instance_owner) {
            rolemask = FLUX_ROLE_OWNER;
            goto success_nolog;
        }
    }
    if (!(f = auth_lookup_rolemask (h, cuid)))
        goto error;
    if (auth_lookup_rolemask_get (f, &rolemask) < 0) {
        flux_future_destroy (f);
        errno = EPERM;
        goto error;
    }
    flux_future_destroy (f);
    if (rolemask == FLUX_ROLE_NONE) {
        flux_log (h, LOG_ERR, "%s: uid=%d no assigned roles",
                  __FUNCTION__, cuid);
        errno = EPERM;
        goto error;
    }
    flux_log (h, LOG_INFO, "%s: uid=%d allowed rolemask=0x%x",
              __FUNCTION__, cuid, rolemask);
success_nolog:
    if (flux_module_debug_test (h, DEBUG_OWNERDROP_ONESHOT, true)
                            && (rolemask & FLUX_ROLE_OWNER)) {
        cred->rolemask = FLUX_ROLE_USER;
        cred->userid = FLUX_USERID_UNKNOWN;
    } else {
        cred->userid = cuid;
        cred->rolemask = rolemask;
    }
    return 0;
error:
    return -1;
}

/* Usock client encouters an error.
 */
static void uconn_error (struct usock_conn *uconn, int errnum, void *arg)
{
    struct connector_local *ctx = arg;

    if (errnum != EPIPE && errnum != EPROTO && errnum != ECONNRESET) {
        const struct auth_cred *cred = usock_conn_get_cred (uconn);
        errno = errnum;
        flux_log_error (ctx->h,
                        "client=%.5s userid=%u",
                        usock_conn_get_uuid (uconn),
                        (unsigned int)cred->userid);
    }
    usock_conn_destroy (uconn);
}

/* Usock client sends message to router.
 */
static void uconn_recv (struct usock_conn *uconn, flux_msg_t *msg, void *arg)
{
    struct router_entry *entry = usock_conn_aux_get (uconn, route_auxkey);

    router_entry_recv (entry, msg);
}

/* Router sends message to a usock client.
 * If event is private, ensure user's credentials allow delivery.
 */
static int uconn_send (const flux_msg_t *msg, void *arg)
{
    struct usock_conn *uconn = arg;
    const struct auth_cred *cred;
    int type;

    if (flux_msg_get_type (msg, &type) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_EVENT:
            cred = usock_conn_get_cred (uconn);
            if (auth_check_event_privacy (msg, cred) < 0)
                return -1;
            break;
        default:
            break;
    }
    return usock_conn_send (uconn, msg);
}

/* Accept a connection from new client.
 * This function must call usock_conn_accept() or usock_conn_reject().
 */
static void acceptor_cb (struct usock_conn *uconn, void *arg)
{
    struct connector_local *ctx = arg;
    const struct auth_cred *initial_cred;
    struct auth_cred cred;
    struct router_entry *entry;

    initial_cred = usock_conn_get_cred (uconn);
    if (client_authenticate (ctx->h,
                             ctx->instance_owner,
                             initial_cred->userid,
                             &cred) < 0)
        goto error;
    if (!(entry = router_entry_add (ctx->router,
                                    usock_conn_get_uuid (uconn),
                                    uconn_send,
                                    uconn)))
        goto error;
    if (usock_conn_aux_set (uconn,
                            route_auxkey,
                            entry,
                            (flux_free_f)router_entry_delete) < 0) {
        router_entry_delete (entry);
        goto error;
    }
    usock_conn_set_error_cb (uconn, uconn_error, ctx);
    usock_conn_set_recv_cb (uconn, uconn_recv, ctx);
    usock_conn_accept (uconn, &cred);
    return;
error:
    usock_conn_reject (uconn, errno);
    usock_conn_destroy (uconn);
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct connector_local ctx;
    const char *local_uri = NULL;
    char *tmpdir;
    const char *sockpath;
    int rc = -1;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    ctx.instance_owner = geteuid ();

    /* Create router
     */
    if (!(ctx.router = router_create (h))) {
        flux_log_error (h, "router_create");
        goto done;
    }

    if (!(local_uri = flux_attr_get (h, "local-uri"))) {
        flux_log_error (h, "flux_attr_get local-uri");
        goto done;
    }
    if (!(tmpdir = strstr (local_uri, "local://"))) {
        flux_log (h, LOG_ERR, "malformed local-uri");
        goto done;
    }
    sockpath = tmpdir + 8;

    /* Create listen socket and watcher to handle new connections
     */
    if (!(ctx.server = usock_server_create (flux_get_reactor (h),
                                            sockpath,
                                            0777))) {
        flux_log_error (h, "%s: cannot set up socket listener", sockpath);
        goto done;
    }
    cleanup_push_string (cleanup_file, sockpath);
    usock_server_set_acceptor (ctx.server, acceptor_cb, &ctx);

    /* Start reactor
     */
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }

    router_mute (ctx.router); // issue #1025 - disable unsub during shutdown
    rc = 0;
done:
    usock_server_destroy (ctx.server); // destroy before router
    router_destroy (ctx.router);
    return rc;
}

MOD_NAME ("connector-local");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
