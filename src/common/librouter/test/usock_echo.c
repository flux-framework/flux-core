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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libtestutil/util.h"
#include "src/common/librouter/usock.h"
#include "ccan/str/str.h"

#include "usock_util.h"

/* Test Server
 *
 * Accept all connections on <tmpdir>.server.
 * Echo messages back to sender.
 * Destroy connection on error callback.
 */

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
    if (usock_conn_send (conn, msg) < 0)
        diag ("usock_conn_send failed: %s", flux_strerror (errno));
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

    diag ("server_acceptor uuid=%.5s", usock_conn_get_uuid (conn));

    usock_conn_set_error_cb (conn, server_error_cb, NULL);
    usock_conn_set_recv_cb (conn, server_recv_cb, NULL);

    usock_conn_accept (conn, cred);
}

/* Print diagnostics each time through the server reactor loop
 */
static void server_prep (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct usock_server *server = arg;
    json_t *stats;
    static int last_connects = 0;
    int connects;

    if ((stats = usock_server_stats_get (server))
        && json_unpack (stats, "{s:i}", "connects", &connects) == 0
        && connects > last_connects) {
        char *s = NULL;

        if ((s = json_dumps (stats, JSON_COMPACT)))
            diag ("%s", s);
        last_connects = connects;
        free (s);
    }
    json_decref (stats);
}

static int server_cb (flux_t *h, void *arg)
{
    flux_reactor_t *r = flux_get_reactor (h);
    char sockpath[PATH_MAX + 1];
    struct usock_server *server;
    flux_watcher_t *w;

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
    usock_server_set_acceptor (server, server_acceptor, NULL);

    if (!(w = flux_prepare_watcher_create (r, server_prep, server)))
        diag ("error creating server prepare watcher for diagnostic stats");
    flux_watcher_start (w);

    if (flux_reactor_run (r, 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    usock_server_destroy (server);
    flux_watcher_destroy (w);
    return 0;
}

/* End Test Server
 */

/* Connect and immediately disconnect.
 * This likely causes the server-side error callback to be made
 * in the context of sending the auth byte.  In early rev of usock,
 * this crashed the server with a zuuid assertion.
 */
static void test_early_disconnect (flux_t *h)
{
    char sockpath[PATH_MAX + 1];
    int fd;

    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/server",
                  tmpdir) >= sizeof (sockpath))
        BAIL_OUT ("buffer overflow");
    fd = usock_client_connect (sockpath, USOCK_RETRY_DEFAULT);
    ok (fd >= 0,
        "usock_client_connect %s works", sockpath);
    /* do nothing */
    diag ("disconnecting");
    (void)close (fd);
}

static bool equal_message (const flux_msg_t *m1, const flux_msg_t *m2)
{
    int type1, type2;
    const char *topic1, *topic2;
    const void *buf1, *buf2;
    size_t len1, len2;

    if (flux_msg_get_type (m1, &type1) < 0)
        return false;
    if (flux_msg_get_type (m2, &type2) < 0)
        return false;
    if (type1 != type2)
        return false;
    if (flux_msg_get_topic (m1, &topic1) < 0)
        return false;
    if (flux_msg_get_topic (m2, &topic2) < 0)
        return false;
    if (!streq (topic1, topic2))
        return false;
    if (flux_msg_has_payload (m1) && !flux_msg_has_payload (m2))
        return false;
    if (!flux_msg_has_payload (m1) && flux_msg_has_payload (m2))
        return false;
    if (flux_msg_has_payload (m1) && flux_msg_has_payload (m2)) {
        if (flux_msg_get_payload (m1, &buf1, &len1) < 0)
            return false;
        if (flux_msg_get_payload (m2, &buf2, &len2) < 0)
            return false;
        if (len1 != len2 || memcmp (buf1, buf2, len1))
            return false;
    }
    return true;
}

/* Send a small message and receive it back.
 * Assumes that the OS socket buffer is sufficient to contain all of it.
 */
static void test_one_echo (flux_t *h)
{
    char sockpath[PATH_MAX + 1];
    flux_msg_t *msg;
    flux_msg_t *rmsg;
    int fd;
    struct usock_client *client;

    if (!(msg = flux_request_encode ("a", NULL)))
        BAIL_OUT ("flux_request_encode failed");

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
        "usock_client_send works");
    ok ((rmsg = usock_client_recv (client, 0)) != NULL,
        "usock_client_recv works");

    ok (equal_message (msg, rmsg),
       "recv message matches sent");

    diag ("disconnecting");

    usock_client_destroy (client);
    (void)close (fd);
    flux_msg_destroy (rmsg);
    flux_msg_destroy (msg);
}

struct async_ctx {
    flux_reactor_t *r;
    flux_msg_t *msg;
    int max_recv;
    int count_recv;
    int errors;
};

void async_recv_cb (struct cli *cli, const flux_msg_t *msg, void *arg)
{
    struct async_ctx *ctx = arg;

    if (equal_message (msg, ctx->msg) == false)
        ctx->errors++;

    if (++ctx->count_recv == ctx->max_recv) {
        ok (ctx->errors == 0,
            "%d recv messages match sent messages", ctx->max_recv);
        flux_reactor_stop (ctx->r);
    }
}

static void test_async_stream (flux_t *h, int size, int count)
{
    char sockpath[PATH_MAX + 1];
    int fd;
    struct cli *cli;
    struct async_ctx ctx;
    char *buf;
    int i;
    int errors;

    memset (&ctx, 0, sizeof (ctx));
    ctx.r = flux_get_reactor (h);
    if (!(buf = malloc (size)))
        BAIL_OUT ("malloc failed");
    memset (buf, 0xf0, size);
    if (!(ctx.msg = flux_request_encode_raw ("a", buf, size)))
        BAIL_OUT ("flux_request_encode failed");
    ctx.max_recv = count;

    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/server",
                  tmpdir) >= sizeof (sockpath))
        BAIL_OUT ("buffer overflow");
    fd = usock_client_connect (sockpath, USOCK_RETRY_DEFAULT);
    if (fd < 0)
        BAIL_OUT ("usock_client_connect failed");
    cli = cli_create (ctx.r, fd, async_recv_cb, &ctx);
    if (!cli)
        BAIL_OUT ("cli_create failed");

    diag ("connected");

    errors = 0;
    for (i = 0; i < count; i++) {
        if (cli_send (cli, ctx.msg) < 0)
            errors++;
    }
    ok (errors == 0,
        "sent %d message size %d", count, size);

    if (flux_reactor_run (ctx.r, 0) < 0)
        BAIL_OUT ("flux_reactor_run returned -1: %s", flux_strerror (errno));

    diag ("disconnecting");

    cli_destroy (cli);
    (void)close (fd);
    free (buf);
    flux_msg_destroy (ctx.msg);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    tmpdir_create ();

    signal (SIGPIPE, SIG_IGN);

    diag ("starting test server");

    if (!(h = test_server_create (0, server_cb, NULL)))
        BAIL_OUT ("test_server_create failed");

    test_early_disconnect (h);
    test_one_echo (h);
    test_async_stream (h, 1024, 1024);
    test_async_stream (h, 4096, 256);
    test_async_stream (h, 16384, 64);
    test_async_stream (h, 1048576, 1);

    diag ("stopping test server");
    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);

    tmpdir_destroy ();
    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
