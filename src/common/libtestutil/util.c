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
#include <czmq.h>
#include <uuid.h>
#include <pthread.h>
#include <sys/poll.h>

#include "util.h"

#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libtap/tap.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif


struct test_server {
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

static flux_t *test_connector_create (const char *shmem_name,
                                      bool server, int flags);


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

flux_t *test_server_create (int cflags, test_server_f cb, void *arg)
{
    int e;
    struct test_server *a;
    int sflags = 0; // server connector flags

    if (!(a = calloc (1, sizeof (*a))))
        BAIL_OUT ("calloc");
    a->cb = cb;
    a->arg = arg;

    uuid_generate (a->uuid);
    uuid_unparse (a->uuid, a->uuid_str);

    if (getenv ("FLUX_HANDLE_TRACE"))
        cflags |= FLUX_O_TRACE;

    /* Create back-to-back wired flux_t handles
     */
    if (!(a->s = test_connector_create (a->uuid_str, true, sflags)))
        BAIL_OUT ("test_connector_create server");
    if (!(a->c = test_connector_create (a->uuid_str, false, cflags)))
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

void test_server_environment_init (const char *test_name)
{
    zsys_init ();
    zsys_set_logstream (stderr);
    zsys_set_logident (test_name);
    zsys_handler_set (NULL);
    zsys_set_linger (5); // msec
}

/* Test connector implementation
 */

struct test_connector {
    zsock_t *sock;
    flux_t *h;
    struct flux_msg_cred cred;
};

static int test_connector_pollevents (void *impl)
{
    struct test_connector *tcon = impl;
    int e = zsock_events (tcon->sock);
    int revents = 0;

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

    return zsock_fd (tcon->sock);
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
            .socket = zsock_resolve (tcon->sock),
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

    zsock_destroy (&tcon->sock);
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

static flux_t *test_connector_create (const char *shmem_name,
                                      bool server, int flags)
{
    struct test_connector *tcon;

    if (!(tcon = calloc (1, sizeof (*tcon))))
        BAIL_OUT ("calloc");
    tcon->cred.userid = getuid ();
    tcon->cred.rolemask = FLUX_ROLE_OWNER;
    if (!(tcon->sock = zsock_new_pair (NULL)))
        BAIL_OUT ("zsock_new_pair");
    zsock_set_unbounded (tcon->sock);
    if (server) {
        if (zsock_bind (tcon->sock, "inproc://%s", shmem_name) < 0)
            BAIL_OUT ("zsock_bind %s", shmem_name);
    }
    else {
        if (zsock_connect (tcon->sock, "inproc://%s", shmem_name) < 0)
            BAIL_OUT ("zsock_connect %s", shmem_name);
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

/* Loopback connector implementation
 */

struct loopback_connector {
    struct flux_msglist *queue;
    flux_t *h;
    int pollfd;
    int pollevents;
    struct flux_msg_cred cred;
};

static int loopback_connector_pollevents (void *impl)
{
    struct loopback_connector *lcon = impl;
    int e;
    int revents = 0;

    if ((e = flux_msglist_pollevents (lcon->queue)) < 0)
        return -1;
    if (e & POLLIN)
        revents |= FLUX_POLLIN;
    if (e & POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & POLLERR)
        revents |= FLUX_POLLERR;

    return revents;
}

static int loopback_connector_pollfd (void *impl)
{
    struct loopback_connector *lcon = impl;

    return flux_msglist_pollfd (lcon->queue);
}

static int loopback_connector_send (void *impl, const flux_msg_t *msg,
                                    int flags)
{
    struct loopback_connector *lcon = impl;
    struct flux_msg_cred cred;
    flux_msg_t *cpy;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (cred.userid == FLUX_USERID_UNKNOWN) {
        if (flux_msg_set_userid (cpy, lcon->cred.userid) < 0)
            goto error;
    }
    if (cred.rolemask == FLUX_ROLE_NONE) {
        if (flux_msg_set_rolemask (cpy, lcon->cred.rolemask) < 0)
            goto error;
    }
    if (flux_msglist_append (lcon->queue, cpy) < 0)
        goto error;
    flux_msg_destroy (cpy);
    return 0;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static flux_msg_t *loopback_connector_recv (void *impl, int flags)
{
    struct loopback_connector *lcon = impl;
    flux_msg_t *msg;

    if (!(msg = (flux_msg_t *)flux_msglist_pop (lcon->queue))) {
        errno = EWOULDBLOCK;
        return NULL;
    }
    return msg;
}

static int loopback_connector_getopt (void *impl, const char *option,
                                      void *val, size_t size)
{
    struct loopback_connector *lcon = impl;

    if (option && streq (option, FLUX_OPT_TESTING_USERID)) {
        if (size != sizeof (lcon->cred.userid))
            goto error;
        memcpy (val, &lcon->cred.userid, size);
    }
    else if (option && streq (option, FLUX_OPT_TESTING_ROLEMASK)) {
        if (size != sizeof (lcon->cred.rolemask))
            goto error;
        memcpy (val, &lcon->cred.rolemask, size);
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int loopback_connector_setopt (void *impl, const char *option,
                                      const void *val, size_t size)
{
    struct loopback_connector *lcon = impl;
    size_t val_size;

    if (option && streq (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (lcon->cred.userid);
        if (size != val_size)
            goto error;
        memcpy (&lcon->cred.userid, val, val_size);
    }
    else if (option && streq (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (lcon->cred.rolemask);
        if (size != val_size)
            goto error;
        memcpy (&lcon->cred.rolemask, val, val_size);
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static void loopback_connector_fini (void *impl)
{
    struct loopback_connector *lcon = impl;

    flux_msglist_destroy (lcon->queue);
    free (lcon);
}

static const struct flux_handle_ops loopback_ops = {
    .pollfd = loopback_connector_pollfd,
    .pollevents = loopback_connector_pollevents,
    .send = loopback_connector_send,
    .recv = loopback_connector_recv,
    .getopt = loopback_connector_getopt,
    .setopt = loopback_connector_setopt,
    .impl_destroy = loopback_connector_fini,
};

flux_t *loopback_create (int flags)
{
    struct loopback_connector *lcon;

    if (!(lcon = calloc (1, sizeof (*lcon))))
        BAIL_OUT ("calloc");
    lcon->cred.userid = getuid ();
    lcon->cred.rolemask = FLUX_ROLE_OWNER;
    if (!(lcon->queue = flux_msglist_create ()))
        BAIL_OUT ("flux_msglist_create");

    if (!(lcon->h = flux_handle_create (lcon, &loopback_ops, flags)))
        BAIL_OUT ("flux_handle_create");
    return lcon->h;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
