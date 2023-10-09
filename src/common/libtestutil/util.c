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
#include <flux/core.h>
#include <zmq.h>
#include <uuid.h>
#include <pthread.h>
#include <sys/poll.h>

#include "util.h"

#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/sockopt.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

struct test_server {
    void *zctx;
    flux_t *c;
    flux_t *s;
    flux_msg_handler_t *shutdown_mh;
    flux_msg_handler_t *diag_mh;
    test_server_f cb;
    void *arg;
    pthread_t thread;
    int rc;
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];
};

static flux_t *test_connector_create (void *zctx,
                                      const char *shmem_name,
                                      bool server,
                                      int flags);


void shutdown_cb (flux_t *h, flux_msg_handler_t *mh,
                  const flux_msg_t *msg, void *arg)
{
    flux_reactor_stop (flux_get_reactor (h));
}

static void *thread_wrapper (void *arg)
{
    struct test_server *a = arg;

    if (a->cb (a->s, a->arg) < 0)
        a->rc = -1;
    else
        a->rc = 0;

    return NULL;
}

int test_server_stop (flux_t *c)
{
    int e;
    flux_msg_t *msg = NULL;
    int rc = -1;
    struct test_server *a = flux_aux_get (c, "test_server");

    if (!a)
        BAIL_OUT ("flux_aux_get test_server");
    if (!(msg = flux_request_encode ("shutdown", NULL)))
        BAIL_OUT ("flux_request_encode");
    if (flux_send (a->c, msg, 0) < 0)
        BAIL_OUT ("flux_send");
    if ((e = pthread_join (a->thread, NULL)) != 0)
        BAIL_OUT ("pthread_join");
    rc = a->rc;
    flux_msg_destroy (msg);
    return rc;
}

static void diag_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    int msgtype;
    const char *topic = NULL;

    if (flux_msg_get_type (msg, &msgtype) < 0)
        goto badmsg;
    if (msgtype != FLUX_MSGTYPE_CONTROL) {
        if (flux_msg_get_topic (msg, &topic) < 0)
            goto badmsg;
    }
    diag ("server: < %s%s%s", flux_msg_typestr (msgtype),
           topic ? " " : "", topic ? topic : "");
    return;
badmsg:
    diag ("server: malformed message:", flux_strerror (errno));
}

static int diag_server (flux_t *h, void *arg)
{
    diag ("server: starting");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    diag ("server: ending");
    return 0;
}

static void test_server_destroy (struct test_server *a)
{
    if (a) {
        flux_msg_handler_destroy (a->shutdown_mh);
        flux_msg_handler_destroy (a->diag_mh);
        flux_close (a->s);
        free (a);
    }
}

flux_t *test_server_create (void *zctx, int cflags, test_server_f cb, void *arg)
{
    int e;
    struct test_server *a;
    int sflags = 0; // server connector flags

    if (!(a = calloc (1, sizeof (*a))))
        BAIL_OUT ("calloc");
    a->cb = cb;
    a->arg = arg;
    a->zctx = zctx;

    uuid_generate (a->uuid);
    uuid_unparse (a->uuid, a->uuid_str);

    if (getenv ("FLUX_HANDLE_TRACE"))
        cflags |= FLUX_O_TRACE;

    /* Create back-to-back wired flux_t handles
     */
    if (!(a->s = test_connector_create (a->zctx, a->uuid_str, true, sflags)))
        BAIL_OUT ("test_connector_create server");
    if (!(a->c = test_connector_create (a->zctx, a->uuid_str, false, cflags)))
        BAIL_OUT ("test_connector_create client");

    /* If no callback, register watcher for all messages so we can log them.
     * N.B. this has to go in before shutdown else shutdown will be masked.
     */
    if (!a->cb) {
        if (!(a->diag_mh = flux_msg_handler_create (a->s, FLUX_MATCH_ANY,
                                                    diag_cb, a)))
            BAIL_OUT ("flux_msg_handler_create");
        flux_msg_handler_start (a->diag_mh);
        a->cb = diag_server;
    }

    /* Register watcher for "shutdown" request on server side
     */
    struct flux_match match = FLUX_MATCH_REQUEST;
    match.topic_glob = "shutdown";
    if (!(a->shutdown_mh = flux_msg_handler_create (a->s, match,
                                                    shutdown_cb, a)))
        BAIL_OUT ("flux_msg_handler_create");
    flux_msg_handler_start (a->shutdown_mh);

    /* Start server thread
     */
    if ((e = pthread_create (&a->thread, NULL, thread_wrapper, a)) != 0)
        BAIL_OUT ("pthread_create");
    if (flux_aux_set (a->c, "test_server", a,
                      (flux_free_f)test_server_destroy) < 0)
        BAIL_OUT ("flux_aux_set");
    return a->c;
}

/* Test connector implementation
 */

struct test_connector {
    void *sock;
    flux_t *h;
    struct flux_msg_cred cred;
};

static int test_connector_pollevents (void *impl)
{
    struct test_connector *tcon = impl;
    int e;
    int revents = 0;

    if (zgetsockopt_int (tcon->sock, ZMQ_EVENTS, &e) < 0)
        return -1;
    if (e & ZMQ_POLLIN)
        revents |= FLUX_POLLIN;
    if (e & ZMQ_POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & ZMQ_POLLERR)
        revents |= FLUX_POLLERR;

    return revents;
}

static int test_connector_pollfd (void *impl)
{
    struct test_connector *tcon = impl;
    int fd;

    if (zgetsockopt_int (tcon->sock, ZMQ_FD, &fd) < 0)
        return -1;
    return fd;
}

static int test_connector_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct test_connector *tcon = impl;
    flux_msg_t *cpy;
    int type;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_set_cred (cpy, tcon->cred) < 0)
        goto error;
    if (flux_msg_get_type (cpy, &type) < 0)
        goto error;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            flux_msg_route_enable (cpy);
            if (flux_msg_route_push (cpy, "test") < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (flux_msg_route_delete_last (cpy) < 0)
                goto error;
            break;
    }
    if (zmqutil_msg_send (tcon->sock, cpy) < 0)
        goto error;
    flux_msg_destroy (cpy);
    return 0;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static flux_msg_t *test_connector_recv (void *impl, int flags)
{
    struct test_connector *tcon = impl;

    if ((flags & FLUX_O_NONBLOCK)) {
        zmq_pollitem_t zp = {
            .events = ZMQ_POLLIN,
            .socket = tcon->sock,
            .revents = 0,
            .fd = -1,
        };
        int n;
        if ((n = zmq_poll (&zp, 1, 0L)) <= 0) {
            if (n == 0)
                errno = EWOULDBLOCK;
            return NULL;
        }
    }
    return zmqutil_msg_recv (tcon->sock);
}

static void test_connector_fini (void *impl)
{
    struct test_connector *tcon = impl;

    zmq_close (tcon->sock);
    free (tcon);
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = test_connector_pollfd,
    .pollevents = test_connector_pollevents,
    .send = test_connector_send,
    .recv = test_connector_recv,
    .getopt = NULL,
    .setopt = NULL,
    .impl_destroy = test_connector_fini,
};

static flux_t *test_connector_create (void *zctx,
                                      const char *shmem_name,
                                      bool server,
                                      int flags)
{
    struct test_connector *tcon;
    char uri[256];

    if (!(tcon = calloc (1, sizeof (*tcon))))
        BAIL_OUT ("calloc");
    tcon->cred.userid = getuid ();
    tcon->cred.rolemask = FLUX_ROLE_OWNER;
    if (!(tcon->sock = zmq_socket (zctx, ZMQ_PAIR))
        || zsetsockopt_int (tcon->sock, ZMQ_SNDHWM, 0) < 0
        || zsetsockopt_int (tcon->sock, ZMQ_RCVHWM, 0) < 0
        || zsetsockopt_int (tcon->sock, ZMQ_LINGER, 5) < 0)
        BAIL_OUT ("zmq_socket failed");
    snprintf (uri, sizeof (uri), "inproc://%s", shmem_name);
    if (server) {
        if (zmq_bind (tcon->sock, uri) < 0)
            BAIL_OUT ("zmq_bind %s", uri);
    }
    else {
        if (zmq_connect (tcon->sock, uri) < 0)
            BAIL_OUT ("zmq_connect %s", uri);
    }
    if (!(tcon->h = flux_handle_create (tcon, &handle_ops, flags)))
        BAIL_OUT ("flux_handle_create");
    /* Allow server to have children
     */
    if (server) {
        flux_reactor_t *r = flux_reactor_create (FLUX_REACTOR_SIGCHLD);
        if (!r || flux_set_reactor (tcon->h, r) < 0)
            BAIL_OUT ("failed to set reactor for flux handle");
        /* Schedule custom reactor for destruction on flux_close() */
        flux_aux_set (tcon->h, NULL, r, (flux_free_f) flux_reactor_destroy);
    }
    return tcon->h;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
