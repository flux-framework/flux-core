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
#include <uuid.h>
#include <pthread.h>
#include <signal.h>

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"

#include "util.h"

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
    char uri[64];

    /* To support libsubprocess's SIGCHLD watcher in the server thread,
     * block SIGCHLD before spawning threads to avoid it being delivered
     * to the client thread occasionally.
     */
    sigset_t sigmask;
    sigemptyset (&sigmask);
    sigaddset (&sigmask, SIGCHLD);
    if (sigprocmask (SIG_BLOCK, &sigmask, NULL) < 0)
        BAIL_OUT ("sigprocmask failed");

    if (!(a = calloc (1, sizeof (*a))))
        BAIL_OUT ("calloc");
    a->cb = cb;
    a->arg = arg;

    uuid_generate (a->uuid);
    uuid_unparse (a->uuid, a->uuid_str);

    if (getenv ("FLUX_HANDLE_TRACE"))
        cflags |= FLUX_O_TRACE;

    /* Create back-to-back wired flux_t handles.
     */
    snprintf (uri, sizeof (uri), "interthread://%s", a->uuid_str);
    if (!(a->s = flux_open (uri, sflags))
        || flux_opt_set (a->s, FLUX_OPT_ROUTER_NAME, "server", 7) < 0)
        BAIL_OUT ("could not create server interthread handle");
    if (!(a->c = flux_open (uri, cflags)))
        BAIL_OUT ("could not create client interthread handle");

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
