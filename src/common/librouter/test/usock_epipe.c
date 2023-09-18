/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <pthread.h>
#include <flux/core.h>
#include <zmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libtestutil/util.h"
#include "src/common/librouter/usock.h"
#include "ccan/str/str.h"

#include "usock_util.h"

/* EPIPE handling test:
 * 
 * Client sends multiple messages to server and immediately closes socket.
 * Server reads messages and tries to echo each back to client with closed
 *  connection.
 * Server tracks count of received messages in shared "test_params"
 *  structure.
 * A global mutex "server_mutex" is locked before tests are run and
 *  unlocked by the server only after each client connection exits,
 *  allowing the test to synchronize with the server
 */

struct test_params {
    int ready;
    int expected;
    int recvd;
};

pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t server_cond = PTHREAD_COND_INITIALIZER;

static char tmpdir[PATH_MAX + 1];

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

    if (topic && streq (topic, "init")) {
        if (flux_msg_unpack (msg, "{s:i}", "expected", &tp->expected) < 0)
            diag ("flux_msg_pack: %s", flux_strerror (errno));
        diag ("connection: uuid=%.5s: expect %d messages",
              usock_conn_get_uuid (conn),
              tp->expected);
    }

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

static void server_close_cb (struct usock_conn *conn, void *arg)
{
    struct test_params *tp = arg;
    diag ("server_close_cb: uuid=%.5s: recvd %d/%d messages",
        usock_conn_get_uuid (conn),
        tp->recvd, tp->expected);
    pthread_mutex_lock (&server_mutex);
    tp->ready = 1;
    pthread_cond_signal (&server_cond);
    pthread_mutex_unlock (&server_mutex);
}

static void server_acceptor (struct usock_conn *conn, void *arg)
{
    const struct flux_msg_cred *cred;

    cred = usock_conn_get_cred (conn);

    usock_conn_set_error_cb (conn, server_error_cb, arg);
    usock_conn_set_recv_cb (conn, server_recv_cb, arg);
    usock_conn_set_close_cb (conn, server_close_cb, arg);

    usock_conn_accept (conn, cred);
}

static int server_cb (flux_t *h, void *arg)
{
    flux_reactor_t *r = flux_get_reactor (h);
    char sockpath[PATH_MAX + 1];
    struct usock_server *server;

    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/server",
                  tmpdir) >= sizeof (sockpath)) {
        diag ("usock_server_create buffer overflow");
        return -1;
    }
    if (!(server = usock_server_create (r, sockpath, 0644))) {
        diag ("usock_server_create failed");
        return -1;
    }
    usock_server_set_acceptor (server, server_acceptor, arg);

    if (flux_reactor_run (r, 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    usock_server_destroy (server);
    return 0;
}

/* End Test Server
 */

/* Wait on condition variable for server to mark test results ready.
 * Then ensure expected messages == recvd messages
 */
static void check_result (struct test_params *tp)
{
    pthread_mutex_lock (&server_mutex);
    while (!tp->ready)
        pthread_cond_wait (&server_cond, &server_mutex);
    ok (tp->expected == tp->recvd,
        "got %d/%d messages",
        tp->recvd,
        tp->expected);
    pthread_mutex_unlock (&server_mutex);
}


/* Send a burst of count small messages and closes connection.
 * Assumes that the OS socket buffer is sufficient to contain all of it.
 */
static void test_send_and_exit (flux_t *h, int count)
{
    int i;
    char sockpath[PATH_MAX + 1];
    flux_msg_t *msg;
    flux_msg_t *nmsg;
    int fd;
    struct usock_client *client;

    if (!(msg = flux_request_encode ("init", NULL))
        || !(nmsg = flux_request_encode ("nil", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (flux_msg_pack (msg, "{s:i}", "expected", count) < 0)
        BAIL_OUT ("flux_msg_pack failed");

    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/server",
                  tmpdir) >= sizeof (sockpath))
        BAIL_OUT ("buffer overflow");
    fd = usock_client_connect (sockpath, USOCK_RETRY_DEFAULT);
    ok (fd >= 0,
        "usock_client_connect %s works", sockpath);
    ok ((client = usock_client_create (fd)) != NULL,
        "usock_client_create works");

    ok (usock_client_send (client, msg, 0) == 0,
        "usock_client_send init message works: expected=%d", count);

    for (i = 1; i < count; i++) {
        ok (usock_client_send (client, nmsg, 0) == 0,
            "usock_client_send[%d] works", i);
    }

    diag ("disconnecting");

    usock_client_destroy (client);
    (void)close (fd);
    flux_msg_destroy (msg);
    flux_msg_destroy (nmsg);
}
int main (int argc, char *argv[])
{
    struct test_params tp = {0};
    flux_t *h;
    void *zctx;

    plan (NO_PLAN);

    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("cannot create zeromq context");

    tmpdir_create ();

    signal (SIGPIPE, SIG_IGN);

    diag ("starting test server");

    if (!(h = test_server_create (zctx, 0, server_cb, &tp)))
        BAIL_OUT ("test_server_create failed");

    test_send_and_exit (h, 1);
    check_result (&tp);
    memset (&tp, 0, sizeof (tp));

    test_send_and_exit (h, 2);
    check_result (&tp);
    memset (&tp, 0, sizeof (tp));

    test_send_and_exit (h, 5);
    check_result (&tp);
    memset (&tp, 0, sizeof (tp));

    test_send_and_exit (h, 7);
    check_result (&tp);
    memset (&tp, 0, sizeof (tp));

    diag ("stopping test server");
    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);

    tmpdir_destroy ();
    zmq_ctx_term (zctx);
    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
