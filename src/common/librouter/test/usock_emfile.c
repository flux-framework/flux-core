/************************************************************  \
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
#include <sys/param.h>
#include <sys/resource.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libtestutil/util.h"
#include "src/common/librouter/usock.h"

#include "usock_util.h"

/* EMFILE test.
 *
 * Start a usock server and allow one client to connect.
 * Then set RLIMIT_NOFILE such that second client can connect,
 * but the server will get EMFILE (e.g. current count + 2).
 *
 * Then allow first client to exit, freeing a few fds and letting
 *  2nd connection succeed.
 *
 * Ensure that both clients connected and successfully sent a message
 *  each, and count the number of times the server exited the reactor
 *  and ensure that count is not unreasonable.
 *
 */

struct test_params {
    int ready;
    int loop;
    int recvd;
};

static char tmpdir[PATH_MAX + 1];
static char sockpath[PATH_MAX + 1];

pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t server_cond = PTHREAD_COND_INITIALIZER;
static int server_ready = 0;

static void tmpdir_destroy (void)
{
    diag ("rm -r %s", tmpdir);
    if (unlink_recursive (tmpdir) < 0)
        BAIL_OUT ("unlink_recursive failed");
}

static void tmpdir_create (void)
{
    const char *tmp = getenv ("TMPDIR");

    if (snprintf (tmpdir,
                  sizeof (tmpdir),
                  "%s/usock.XXXXXXX",
                  tmp ? tmp : "/tmp") >= sizeof (tmpdir))
        BAIL_OUT ("tmpdir_create buffer overflow");
    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp %s: %s", tmpdir, strerror (errno));
    diag ("mkdir %s", tmpdir);
}

static void server_recv_cb (struct usock_conn *conn, flux_msg_t *msg, void *arg)
{
    struct test_params *tp = arg;
    const char *topic = NULL;

    if (flux_request_decode (msg, &topic, NULL) < 0)
        diag ("usock_conn_send failed: %s", flux_strerror (errno));

    if (usock_conn_send (conn, msg) < 0)
        diag ("usock_conn_send failed: %s", flux_strerror (errno));

    tp->recvd++;
}

static void server_error_cb (struct usock_conn *conn, int errnum, void *arg)
{
    diag ("server_error_cb uuid=%.5s: %s",
         usock_conn_get_uuid (conn),
         flux_strerror (errnum));

    usock_conn_destroy (conn);
}

static void server_acceptor (struct usock_conn *conn, void *arg)
{
    const struct flux_msg_cred *cred;

    cred = usock_conn_get_cred (conn);

    usock_conn_set_error_cb (conn, server_error_cb, arg);
    usock_conn_set_recv_cb (conn, server_recv_cb, arg);

    usock_conn_accept (conn, cred);
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct test_params *params = arg;
    params->loop++;
}

static int server_cb (flux_t *h, void *arg)
{
    flux_watcher_t *w;
    flux_reactor_t *r = flux_get_reactor (h);
    struct usock_server *server;
    struct test_params *tp = arg;

    if (!(server = usock_server_create (r, sockpath, 0644))) {
        diag ("usock_server_create failed");
        return -1;
    }
    usock_server_set_acceptor (server, server_acceptor, tp);

    if (!(w = flux_check_watcher_create (r, check_cb, tp))) {
        diag ("flux_check_watcher_create failed");
        return -1;
    }
    flux_watcher_start (w);

    pthread_mutex_lock (&server_mutex);
    server_ready = 1;
    pthread_cond_signal (&server_cond);
    pthread_mutex_unlock (&server_mutex);

    if (flux_reactor_run (r, 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    flux_watcher_destroy (w);
    usock_server_destroy (server);
    return 0;
}

/* End Test Server
 */

/*  Client thread - create a connection, send a message, pause a configurable
 *   amount, then exit.
 */
struct client_args {
    pthread_t thread;
    int id;
    int ready;
    int done;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static void *client_thread (void *arg)
{
    int fd;
    flux_msg_t *msg;
    struct usock_client *client;
    struct client_args *args = arg;

    /*  Due to raciness with server, usock_client_connect() may fail due
     *  to ENFILE. Just retry until condition resolves itself.
     */
    int retries = 5;
    while ((fd = usock_client_connect (sockpath, USOCK_RETRY_DEFAULT)) < 0
           && retries--)
        usleep (1000);

    if (fd < 0)
        BAIL_OUT ("usock_client_connect: %s", strerror (errno));

    if ((client = usock_client_create (fd)) == NULL)
        BAIL_OUT ("usock_client_create");
   if (!(msg = flux_request_encode ("nil", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (usock_client_send (client, msg, 0) < 0)
        BAIL_OUT ("client %d: usock_client_send message works", args->id);

    /* Signal main thread we're ready */
    pthread_mutex_lock (&args->mutex);
    args->ready = 1;
    pthread_cond_signal (&args->cond);
    pthread_mutex_unlock (&args->mutex);

    /*  Wait for test program to tell this client to exit
     */
    pthread_mutex_lock (&args->mutex);
    while (!args->done)
        pthread_cond_wait (&args->cond, &args->mutex);
    pthread_mutex_unlock (&args->mutex);

    diag ("client %d: disconnecting", args->id);
    usock_client_destroy (client);
    (void) close (fd);
    flux_msg_destroy (msg);

    return NULL;
}


static void fd_count (void *arg, int fd)
{
    int *countp = arg;
    (*countp)++;
}

static int fds_inuse (void)
{
    int count = 0;
    if (fdwalk (fd_count, &count))
        BAIL_OUT ("fdwalk");
    return count;
}

static void wait_for_server (void)
{
    pthread_mutex_lock (&server_mutex);
    while (!server_ready)
        pthread_cond_wait (&server_cond, &server_mutex);
    server_ready = 0;
    pthread_mutex_unlock (&server_mutex);
}

static void wait_for_client (struct client_args *a)
{
    pthread_mutex_lock (&a->mutex);
    while (!a->ready)
        pthread_cond_wait (&a->cond, &a->mutex);
    pthread_mutex_unlock (&a->mutex);
}

static void wait_for_client_complete (struct client_args *a)
{
        pthread_mutex_lock (&a->mutex);
        a->done = 1;
        pthread_cond_signal (&a->cond);
        pthread_mutex_unlock (&a->mutex);
        pthread_join (a->thread, NULL);
}

int main (int argc, char *argv[])
{
    int e;
    struct test_params tp = {0};
    struct client_args cargs[2];
    flux_t *h;

    plan (NO_PLAN);

    tmpdir_create ();
    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/server",
                  tmpdir) >= sizeof (sockpath))
        BAIL_OUT ("Failed to create server sockpath");

    signal (SIGPIPE, SIG_IGN);

    diag ("starting test server");

    if (!(h = test_server_create (0, server_cb, &tp)))
        BAIL_OUT ("test_server_create failed");

    wait_for_server ();

    diag ("fds_inuse = %d", fds_inuse ());

    for (int i = 0; i < 2; i++) {
        struct client_args *a = &cargs[i];
        memset (a, 0, sizeof (struct client_args));
        e = pthread_mutex_init (&a->mutex, NULL);
        if (e != 0)
            BAIL_OUT ("pthread_mutex_init failed: %s", strerror (e));
        e = pthread_cond_init (&a->cond, NULL);
        if (e != 0)
            BAIL_OUT ("pthread_cond_init failed: %s", strerror (e));
        a->id = i;
        a->done = 0;

        e = pthread_create (&a->thread, NULL, client_thread, a);
        if (e != 0)
            BAIL_OUT ("pthread_create client %d failed: %s", i, strerror (e));

        if (i == 0) {
            struct rlimit rlim;

            /*  Wait for first client to connect, then decrease number of
             *   open files to current + 2, leaving the next client
             *   to hang due to EMFILE on the server side.
             */
            wait_for_client (a);
            diag ("client0 started");
            diag ("fds_inuse = %d", fds_inuse ());

            rlim.rlim_cur = fds_inuse () + 2;
            rlim.rlim_max = rlim.rlim_cur;
            diag ("setting nofile limit to %d", rlim.rlim_cur);
            if (setrlimit (RLIMIT_NOFILE, &rlim) < 0)
                BAIL_OUT ("setrlimit: %s", strerror (errno));
        }

    }
    diag ("fds_inuse = %d", fds_inuse ());
    //diag ("usleep 10000");
    usleep (10000);
    diag ("fds_inuse = %d", fds_inuse ());

    for (int i = 0; i < 2; i++) {
        wait_for_client_complete (&cargs[i]);
        diag ("client%d done", i);
    }

    diag ("stopping test server");
    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);

    diag ("results: %d recvd %d loops",
          tp.recvd, tp.loop);

    ok (tp.recvd == 2,
        "got expected messages from two clients");
    ok (tp.loop < 20,
        "number of loops is not unreasonable");

    tmpdir_destroy ();

    diag ("fds_inuse = %d", fds_inuse ());
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
