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
#include <inttypes.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/librouter/usock.h"
#include "src/common/librouter/router.h"
#include "src/broker/module.h"

enum {
    DEBUG_AUTHFAIL_ONESHOT = 1, /* force auth to fail one time */
    DEBUG_OWNERDROP_ONESHOT = 4,/* drop OWNER role to USER on next connection */
};

struct connector_local {
    struct usock_server *server;
    struct router *router;
    flux_t *h;
    uid_t instance_owner;
    int allow_guest_user;
    int allow_root_owner;
    flux_msg_handler_t **handlers;
};

/* A 'struct route_entry' is attached to the 'struct usock_conn' aux hash
 * so that when the client is destroyed, its route is also destroyed.
 * This also helps bridge uconn_recv() to router_entry_recv().
 */
static const char *route_auxkey = "flux::route";


static int client_authenticate (struct connector_local *ctx,
                                uid_t cuid,
                                struct flux_msg_cred *cred)
{
    uint32_t rolemask = FLUX_ROLE_NONE;

    /* Test hook: when set, deny one connection.
     */
    if (flux_module_debug_test (ctx->h, DEBUG_AUTHFAIL_ONESHOT, true)) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "connect by uid=%d denied by debug flag",
                  cuid);
        errno = EPERM;
        goto error;
    }
    /* Assign roles based on connecting uid and configured policy.
     */
    if (cuid == ctx->instance_owner)
        rolemask = FLUX_ROLE_OWNER;
    else if (ctx->allow_root_owner && cuid == 0)
        rolemask = FLUX_ROLE_OWNER;
    else if (ctx->allow_guest_user)
        rolemask = FLUX_ROLE_USER;

    if (rolemask == FLUX_ROLE_NONE) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s: uid=%d no assigned roles",
                  __FUNCTION__,
                  cuid);
        errno = EPERM;
        goto error;
    }
    /* Tack on FLUX_ROLE_LOCAL to indicate that this message was
     * accepted by the local connector.  This role is cleared when
     * the message is received by another broker.
     */
    rolemask |= FLUX_ROLE_LOCAL;
    /* Test hook: drop owner cred for one connection.
     */
    if (flux_module_debug_test (ctx->h, DEBUG_OWNERDROP_ONESHOT, true)) {
        if ((rolemask & FLUX_ROLE_OWNER)) {
            rolemask = FLUX_ROLE_USER;
            cuid = FLUX_USERID_UNKNOWN;
        }
    }

    cred->userid = cuid;
    cred->rolemask = rolemask;
    return 0;
error:
    return -1;
}

/* Usock client encounters an error.
 */
static void uconn_error (struct usock_conn *uconn, int errnum, void *arg)
{
    struct connector_local *ctx = arg;

    if (errnum != EPIPE && errnum != EPROTO && errnum != ECONNRESET) {
        const struct flux_msg_cred *cred = usock_conn_get_cred (uconn);
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
    const struct flux_msg_cred *cred;
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
    const struct flux_msg_cred *initial_cred;
    struct flux_msg_cred cred;
    struct router_entry *entry;

    initial_cred = usock_conn_get_cred (uconn);
    if (client_authenticate (ctx,
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

/* Parse [access] table.
 * Access policy is instance owner only, unless configured otherwise:
 *
 * allow-guest-user = true
 *   Allow users other than instance owner to connect with FLUX_ROLE_USER
 *
 * allow-root-owner = true
 *   Allow root user to have instance owner role
 *
 * Missing [access] keys are interpreted as false.
 * [access] keys other than the above are not allowed.
 */
static int parse_config (struct connector_local *ctx,
                         const flux_conf_t *conf,
                         flux_error_t *errp)
{
    flux_error_t error;
    int allow_guest_user = 0;
    int allow_root_owner = 0;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?b s?b !}}",
                          "access",
                            "allow-guest-user",
                            &allow_guest_user,
                            "allow-root-owner",
                            &allow_root_owner) < 0) {
        errprintf (errp,
                   "error parsing [access] configuration: %s",
                   error.text);
        return -1;
    }
    ctx->allow_guest_user = allow_guest_user;
    ctx->allow_root_owner = allow_root_owner;
    flux_log (ctx->h,
              LOG_DEBUG,
              "allow-guest-user=%s",
              ctx->allow_guest_user ? "true" : "false");
    flux_log (ctx->h,
              LOG_DEBUG,
              "allow-root-owner=%s",
              ctx->allow_root_owner ? "true" : "false");
    return 0;
}

static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct connector_local *ctx = arg;
    const flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0)
        goto error;
    if (parse_config (ctx, conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (flux_set_conf_new (h, flux_conf_incref (conf)) < 0) {
        errstr = "error updating cached configuration";
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "connector-local.config-reload", reload_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static int mod_main (flux_t *h, int argc, char **argv)
{
    struct connector_local ctx;
    const char *local_uri = NULL;
    char *tmpdir;
    const char *sockpath;
    flux_error_t error;
    int rc = -1;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    ctx.instance_owner = getuid ();

    /* Parse configuration
     */
    if (parse_config (&ctx, flux_get_conf (h), &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto done;
    }

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
        errno = EINVAL;
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

    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0)
        goto done;

    /* Start reactor
     */
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }

    router_mute (ctx.router); // issue #1025 - disable unsub during shutdown
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    usock_server_destroy (ctx.server); // destroy before router
    router_destroy (ctx.router);
    return rc;
}

struct module_builtin builtin_connector_local = {
    .name = "connector-local",
    .main = mod_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
