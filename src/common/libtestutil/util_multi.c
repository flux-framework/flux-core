/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <errno.h>

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"

#include "util_multi.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

/* Multi-rank test server.
 *
 * 'size' subprocess-capable servers are created, each on its own back-to-back
 * handle, plus one more pair facing the caller.  A router forwards each
 * message the caller sends to the server its nodeid names, and each message a
 * server sends back to the caller.  The interthread connector is strictly a
 * pair, so this routing layer is what lets a test address more than one rank.
 *
 * Every server shares one thread and one reactor.  That is required, not
 * incidental: libsubprocess reaps children through a process-global SIGCHLD
 * context whose watcher lives on whichever reactor initializes it first, and
 * whose pid table is a plain hash.  One reactor keeps the watcher live for
 * every server and the table single-threaded; one server per thread would
 * race on both.
 */
struct test_server_multi {
    int size;
    flux_t **cli;           // router side of each rank's pair
    flux_t **srv;           // server side of each rank's pair
    flux_t *c;              // caller's handle
    flux_t *s;              // router side of the caller's pair
    flux_reactor_t *r;
    flux_msg_handler_t **mh; // size + 1 handlers: ranks, then caller
    pthread_t thread;
    char uuid_str[UUID_STR_LEN];
};

/* A message from the caller: forward it to the rank its nodeid names.
 */
static void multi_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct test_server_multi *m = arg;
    uint32_t nodeid;
    const char *topic = NULL;

    (void)flux_msg_get_topic (msg, &topic);
    if (topic && streq (topic, "shutdown")) {
        flux_reactor_stop (m->r);
        return;
    }
    /* The caller sits where a client of the rank 0 broker would, so there is
     * no parent to send upstream to.  flux_rpc() turns FLUX_NODEID_UPSTREAM
     * into this flag plus the sender's rank, so test the flag.
     */
    if (flux_msg_has_flag (msg, FLUX_MSGFLAG_UPSTREAM)) {
        if (flux_respond_error (m->s, msg, EHOSTUNREACH, NULL) < 0)
            diag ("test_server_multi: respond EHOSTUNREACH: %s",
                  flux_strerror (errno));
        return;
    }
    if (flux_msg_get_nodeid (msg, &nodeid) < 0)
        nodeid = FLUX_NODEID_ANY;
    if (nodeid == FLUX_NODEID_ANY)
        nodeid = 0;
    /* An out of range rank is unroutable, which a real instance reports
     * the same way as an upstream request with no parent.
     */
    if (nodeid >= (uint32_t)m->size) {
        if (flux_respond_error (m->s, msg, EHOSTUNREACH, NULL) < 0)
            diag ("test_server_multi: respond EHOSTUNREACH: %s",
                  flux_strerror (errno));
        return;
    }
    if (flux_send (m->cli[nodeid], msg, 0) < 0)
        diag ("test_server_multi: forward to rank %lu: %s",
              (unsigned long)nodeid,
              flux_strerror (errno));
}

/* A message from a rank: forward it back to the caller.
 */
static void multi_response_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct test_server_multi *m = arg;

    if (flux_send (m->s, msg, 0) < 0)
        diag ("test_server_multi: forward to client: %s",
              flux_strerror (errno));
}

static void *multi_thread_wrapper (void *arg)
{
    struct test_server_multi *m = arg;

    if (flux_reactor_run (m->r, 0) < 0)
        diag ("test_server_multi: flux_reactor_run failed");
    return NULL;
}

int test_server_stop_multi (flux_t *c)
{
    struct test_server_multi *m = flux_aux_get (c, "test_server_multi");
    flux_msg_t *msg;
    int e;

    if (!m)
        BAIL_OUT ("flux_aux_get test_server_multi");

    if (!(msg = flux_request_encode ("shutdown", NULL)))
        BAIL_OUT ("flux_request_encode");
    if (flux_send (m->c, msg, 0) < 0)
        BAIL_OUT ("flux_send");
    flux_msg_destroy (msg);
    if ((e = pthread_join (m->thread, NULL)) != 0)
        BAIL_OUT ("pthread_join");
    return 0;
}

static void test_server_multi_destroy (struct test_server_multi *m)
{
    if (m) {
        int i;
        if (m->mh) {
            for (i = 0; i < m->size + 1; i++)
                flux_msg_handler_destroy (m->mh[i]);
            free (m->mh);
        }
        /* Closing a server handle runs the aux destructors the setup
         * callback registered on it, tearing down its server.
         */
        if (m->srv) {
            for (i = 0; i < m->size; i++)
                flux_close (m->srv[i]);
            free (m->srv);
        }
        if (m->cli) {
            for (i = 0; i < m->size; i++)
                flux_close (m->cli[i]);
            free (m->cli);
        }
        flux_close (m->s);
        flux_reactor_destroy (m->r);
        free (m);
    }
}

static void set_rank_size (flux_t *h, int rank, int size)
{
    char buf[16];

    snprintf (buf, sizeof (buf), "%d", rank);
    if (flux_attr_set_cacheonly (h, "rank", buf) < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank");
    snprintf (buf, sizeof (buf), "%d", size);
    if (flux_attr_set_cacheonly (h, "size", buf) < 0)
        BAIL_OUT ("flux_attr_set_cacheonly size");
}

flux_t *test_server_create_multi (int size,
                                  int cflags,
                                  test_server_setup_f cb,
                                  void *arg)
{
    struct test_server_multi *m;
    uuid_t uuid;
    char uri[64];
    int e;
    int i;

    if (size < 1 || !cb)
        BAIL_OUT ("test_server_create_multi: invalid argument");

    /* As in test_server_create(): keep SIGCHLD off the caller's thread so
     * libsubprocess's watcher in the server thread receives it.
     */
    sigset_t sigmask;
    sigemptyset (&sigmask);
    sigaddset (&sigmask, SIGCHLD);
    if (sigprocmask (SIG_BLOCK, &sigmask, NULL) < 0)
        BAIL_OUT ("sigprocmask failed");

    if (!(m = calloc (1, sizeof (*m)))
        || !(m->cli = calloc (size, sizeof (m->cli[0])))
        || !(m->srv = calloc (size, sizeof (m->srv[0])))
        || !(m->mh = calloc (size + 1, sizeof (m->mh[0]))))
        BAIL_OUT ("calloc");
    m->size = size;

    if (!(m->r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create");

    /* Caller-facing pair.
     */
    uuid_generate (uuid);
    uuid_unparse (uuid, m->uuid_str);
    if (getenv ("FLUX_HANDLE_TRACE"))
        cflags |= FLUX_O_TRACE;
    snprintf (uri, sizeof (uri), "interthread://%s", m->uuid_str);
    if (!(m->s = flux_open (uri, 0))
        || flux_opt_set (m->s, FLUX_OPT_ROUTER_NAME, "router", 7) < 0)
        BAIL_OUT ("could not create router interthread handle");
    if (!(m->c = flux_open (uri, cflags)))
        BAIL_OUT ("could not create client interthread handle");
    flux_set_reactor (m->s, m->r);
    set_rank_size (m->c, 0, size);

    /* One pair per rank, all on the shared reactor.
     */
    for (i = 0; i < size; i++) {
        uuid_t u;
        char ustr[UUID_STR_LEN];

        uuid_generate (u);
        uuid_unparse (u, ustr);
        snprintf (uri, sizeof (uri), "interthread://%s", ustr);
        if (!(m->srv[i] = flux_open (uri, 0))
            || flux_opt_set (m->srv[i], FLUX_OPT_ROUTER_NAME, "server", 7) < 0)
            BAIL_OUT ("could not create rank server handle");
        if (!(m->cli[i] = flux_open (uri, 0)))
            BAIL_OUT ("could not create rank client handle");
        /* Set the reactor before 'cb' runs, not after.  A callback that
         * creates a subprocess server initializes the process-global SIGCHLD
         * context with flux_get_reactor() of this handle, and only the first
         * caller binds the watcher.  Were the handle still on its own default
         * reactor here, that watcher would land on a reactor nothing runs and
         * no child would ever be reaped.
         */
        flux_set_reactor (m->srv[i], m->r);
        flux_set_reactor (m->cli[i], m->r);
        set_rank_size (m->srv[i], i, size);
        if (cb (m->srv[i], arg) < 0)
            BAIL_OUT ("test_server_create_multi: setup callback failed");
        if (!(m->mh[i] = flux_msg_handler_create (m->cli[i],
                                                  FLUX_MATCH_ANY,
                                                  multi_response_cb,
                                                  m)))
            BAIL_OUT ("flux_msg_handler_create");
        flux_msg_handler_start (m->mh[i]);
    }

    if (!(m->mh[size] = flux_msg_handler_create (m->s,
                                                 FLUX_MATCH_ANY,
                                                 multi_request_cb,
                                                 m)))
        BAIL_OUT ("flux_msg_handler_create");
    flux_msg_handler_start (m->mh[size]);

    if ((e = pthread_create (&m->thread, NULL, multi_thread_wrapper, m)) != 0)
        BAIL_OUT ("pthread_create");
    if (flux_aux_set (m->c,
                      "test_server_multi",
                      m,
                      (flux_free_f)test_server_multi_destroy) < 0)
        BAIL_OUT ("flux_aux_set");
    return m->c;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
