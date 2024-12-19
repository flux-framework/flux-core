/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"
#include "usock.h"

#include "usock_service.h"

static void service_destroy (void *impl);

static const struct flux_handle_ops service_handle_ops;

struct service {
    bool verbose;
    struct usock_server *usock_srv;
    struct flux_msg_cred cred;
    zhashx_t *connections; // uconn by uuid
    flux_t *h;
};

/* zhashx_destructor_fn signature */
static void connection_destructor (void **item)
{
    if (item) {
        usock_conn_destroy (*item);
        *item = NULL;
    }
}

static void notify_disconnect (struct service *ss, const char *uuid)
{
    flux_msg_t *msg;

    /* flux_msg_route_enable() returns void.  To avoid creating an
     * extra branch, call with C trick to avoid breaking up single if
     * statement into multiple.
     */
    if (!(msg = flux_request_encode ("disconnect", NULL))
        || flux_msg_set_noresponse (msg) < 0
        || (flux_msg_route_enable (msg), false)
        || flux_msg_set_cred (msg, ss->cred) < 0
        || flux_msg_route_push (msg, uuid) < 0
        || flux_requeue (ss->h, msg, FLUX_RQ_TAIL) < 0) {
        if (ss->verbose)
            log_msg ("error notifying server of %.5s disconnect", uuid);
    }
    flux_msg_decref (msg);
}

/* usock_conn_error_f signature */
static void service_error (struct usock_conn *uconn, int errnum, void *arg)
{
    struct service *ss = arg;
    const char *uuid = usock_conn_get_uuid (uconn);

    if (ss->verbose) {
        if (errnum != EPIPE && errnum != EPROTO && errnum != ECONNRESET) {
            const struct flux_msg_cred *cred = usock_conn_get_cred (uconn);
            log_errn (errnum, "client=%.5s userid=%u",
                     uuid,
                     (unsigned int)cred->userid);
        }
        log_msg ("bye %.5s", uuid);
    }
    notify_disconnect (ss, uuid); // notify server of disconnect
    zhashx_delete (ss->connections, uuid);
}

/* usock_conn_recv_f signature */
static void service_recv (struct usock_conn *uconn, flux_msg_t *msg, void *arg)
{
    const char *uuid = usock_conn_get_uuid (uconn);
    struct service *ss = arg;
    int type = 0;

    /* flux_msg_route_enable() returns void.  To avoid creating an
     * extra branch, call with C trick to avoid breaking up single if
     * statement into multiple.
     */
    if (flux_msg_get_type (msg, &type) < 0
        || type != FLUX_MSGTYPE_REQUEST
        || (flux_msg_route_enable (msg), false)
        || flux_msg_set_cred (msg, ss->cred) < 0
        || flux_msg_route_push (msg, uuid) < 0
        || flux_requeue (ss->h, msg, FLUX_RQ_TAIL) < 0)
        goto drop;
    return;
drop:
    if (ss->verbose)
        log_msg ("drop %s from %.5s", flux_msg_typestr (type), uuid);
}

/* usock_acceptor_f signature */
static void service_acceptor (struct usock_conn *uconn, void *arg)
{
    struct service *ss = arg;
    const struct flux_msg_cred *cred = usock_conn_get_cred (uconn);
    const char *uuid = usock_conn_get_uuid (uconn);

    if (cred->userid != ss->cred.userid) {
        errno = EPERM;
        goto error;
    }
    if (zhashx_insert (ss->connections, uuid, uconn) < 0) {
        errno = EEXIST;
        goto error;
    }
    if (ss->verbose)
        log_msg ("hi %.5s", uuid);
    usock_conn_set_error_cb (uconn, service_error, ss);
    usock_conn_set_recv_cb (uconn, service_recv, ss);
    usock_conn_accept (uconn, cred);
    return;
error:
    usock_conn_reject (uconn, errno);
    usock_conn_destroy (uconn);
}

/* flux_handle_ops getopt signature */
static int service_getopt (void *impl,
                           const char *option,
                           void *val,
                           size_t size)
{
    struct service *ss = impl;
    if (streq (option, "flux::listen_watcher")) {
        flux_watcher_t *w = usock_server_listen_watcher (ss->usock_srv);
        if (size != sizeof (w))
            goto error;
        memcpy (val, &w, size);
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

/* flux_handle_ops send signature */
static int service_handle_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct service *ss = impl;
    int type = 0;
    flux_msg_t *cpy = NULL;
    const char *uuid;
    struct usock_conn *uconn;

    if (flux_msg_get_type (msg, &type) < 0)
        return -1;
    if (type != FLUX_MSGTYPE_RESPONSE) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (!(uuid = flux_msg_route_last (cpy))) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_set_cred (cpy, ss->cred) < 0)
        goto error;
    if (!(uconn = zhashx_lookup (ss->connections, uuid))) {
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_route_delete_last (cpy) < 0)
        goto error;
    if (usock_conn_send (uconn, cpy) < 0)
        goto error;
    flux_msg_decref (cpy);
    return 0;
error:
    flux_msg_decref (cpy);
    return -1;
}

static struct service *service_create  (flux_reactor_t *r,
                                        const char *sockpath,
                                        bool verbose)
{
    struct service *ss;

    if (!(ss = calloc (1, sizeof (*ss))))
        return NULL;
    ss->verbose = verbose;
    if (!(ss->usock_srv = usock_server_create (r, sockpath, 0777)))
        goto error;
    usock_server_set_acceptor (ss->usock_srv, service_acceptor, ss);
    if (!(ss->connections = zhashx_new ()))
        goto error;
    zhashx_set_key_duplicator (ss->connections, NULL);
    zhashx_set_key_destructor (ss->connections, NULL);
    zhashx_set_destructor (ss->connections, connection_destructor);
    ss->cred.userid = getuid ();
    ss->cred.rolemask = FLUX_ROLE_OWNER;
    return ss;
error:
    service_destroy (ss);
    return NULL;
}

/* flux_handle_ops impl_destroy signature */
static void service_destroy (void *impl)
{
    struct service *ss = impl;
    if (ss) {
        int saved_errno = errno;
        zhashx_destroy (&ss->connections);
        usock_server_destroy (ss->usock_srv);
        free (ss);
        errno = saved_errno;
    }
}

flux_t *usock_service_create (flux_reactor_t *r,
                              const char *rundir,
                              bool verbose)
{
    struct service *ss;

    if (!(ss = service_create (r, rundir, verbose)))
        return NULL;
    if (!(ss->h = flux_handle_create (ss, &service_handle_ops, 0))
            || flux_set_reactor (ss->h, r) < 0) {
        service_destroy (ss);
        return NULL;
    }
    return ss->h;
}

flux_watcher_t *usock_service_listen_watcher (flux_t *h)
{
    flux_watcher_t *w;

    if (flux_opt_get (h, "flux::listen_watcher", &w, sizeof (w)) < 0)
        return NULL;
    return w;
}

static const struct flux_handle_ops service_handle_ops = {
    .send = service_handle_send,
    .impl_destroy = service_destroy,
    .getopt = service_getopt,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
