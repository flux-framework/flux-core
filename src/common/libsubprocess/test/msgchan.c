
/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <fcntl.h>
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "msgchan.h"

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1) {
            count++;
        }
    }
    return count;
}

void diag_stats (struct msgchan *mch)
{
    json_t *stats = NULL;
    char *s = NULL;

    if (msgchan_get_stats (mch, &stats) == 0)
        s = json_dumps (stats, JSON_COMPACT);
    diag ("%s", s ? s : "NULL");
    free (s);
    json_decref (stats);
}

void unique_interthread_uri (char *buf, size_t size)
{
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);
    snprintf (buf, size, "interthread://%s", uuid_str);
}

void test_basic (flux_reactor_t *r)
{
    int rc;
    char uri[128];
    struct msgchan *mch;
    flux_error_t error;
    int fd;
    const char *fduri;
    flux_t *h;
    flux_t *hfd;
    flux_msg_t *testmsg;
    flux_msg_t *msg;

    unique_interthread_uri (uri, sizeof (uri));
    testmsg = flux_request_encode ("foo", NULL);
    if (!testmsg)
        BAIL_OUT ("could not create test message");

    errno = 0;
    error.text[0] = '\0';
    ok (msgchan_create (NULL, uri, &error) == NULL
        && errno == EINVAL
        && strlen (error.text) > 0,
        "msgchan_create reactor=NULL fails with EINVAL and message");

    errno = 0;
    error.text[0] = '\0';
    ok (msgchan_create (r, NULL, &error) == NULL
        && errno == EINVAL
        && strlen (error.text) > 0,
        "msgchan_create uri=NULL fails with EINVAL and message");

    mch = msgchan_create (r, uri, &error);
    ok (mch != NULL,
        "msgchan_create works");
    if (!mch)
        BAIL_OUT ("cannot continue without message channel");
    h = flux_open (uri, 0);
    if (!h)
        BAIL_OUT ("flux_open %s: %s", strerror (errno));

    fduri = msgchan_get_uri (mch);
    ok (fduri != NULL,
        "msgchan_get_uri works");
    diag ("%s", fduri);
    fd = msgchan_get_fd (mch);
    ok (fduri != NULL,
        "msgchan_get_fd works");
    diag ("%d", fd);

    hfd = flux_open (fduri, 0);
    ok (hfd != NULL,
        "fd uri is opened %s (simulating subproc)", fduri);

    // send to subproc
    rc = flux_send (h, testmsg, 0);
    ok (rc == 0,
        "message is sent to subproc");

    rc = flux_reactor_run (r, FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "reactor runs once to transfer data");

    msg = flux_recv (hfd, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "message is received by subproc");
    flux_msg_decref (msg);

    // recv from subproc
    rc = flux_send (hfd, testmsg, 0);
    ok (rc == 0,
        "message is sent from subproc");

    rc = flux_reactor_run (r, FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "reactor runs once to transfer data");

    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "message is received from subproc");
    flux_msg_decref (msg);

    flux_msg_decref (testmsg);
    flux_close (hfd);
    flux_close (h);
    diag_stats (mch);
    msgchan_destroy (mch);

    rc = flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    ok (rc == 0,
        "msgchan_destroy does not leak active watchers");
}

typedef enum { SEND_TO_SUBPROC, RECV_FROM_SUBPROC} bulk_direction_t;

struct bulk_test {
    char uri[128];
    flux_msg_t *testmsg;
    struct msgchan *mch;
    flux_t *h_send;
    flux_t *h_recv;
    flux_watcher_t *w_recv;
    flux_watcher_t *w_send;
    int count;
    int send_count;
    int recv_count;
};

void bulk_send_cb (flux_reactor_t *r,
                   flux_watcher_t *w,
                   int revents,
                   void *arg)
{
    struct bulk_test *ctx = arg;

    if (ctx->send_count < ctx->count) {
        if (flux_send (ctx->h_send, ctx->testmsg, FLUX_O_NONBLOCK) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                diag ("flux_send: %s", strerror (errno));
                flux_reactor_stop_error (r);
            }
            diag ("flux_send got EAGAIN");
            return;
        }
        ctx->send_count++;
    }
    if (ctx->send_count == ctx->count)
        flux_watcher_stop (w);
}

void bulk_recv_cb (flux_reactor_t *r,
                   flux_watcher_t *w,
                   int revents,
                   void *arg)
{
    struct bulk_test *ctx = arg;
    flux_msg_t *msg;

    while (ctx->recv_count < ctx->count) {
        if (!(msg = flux_recv (ctx->h_recv, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                diag ("flux_recv: %s", strerror (errno));
                flux_reactor_stop_error (r);
            }
            return;
        }
        flux_msg_decref (msg);
        ctx->recv_count++;
    }
    /* N.B. end the test by stopping all the watchers */
    if (ctx->recv_count == ctx->count) {
        flux_watcher_stop (w);
        diag_stats (ctx->mch);
        msgchan_destroy (ctx->mch);
        ctx->mch = NULL;
    }
}

void bulk_fini (struct bulk_test *ctx)
{
    flux_watcher_destroy (ctx->w_send);
    flux_watcher_destroy (ctx->w_recv);
    flux_close (ctx->h_send);
    flux_close (ctx->h_recv);
    if (ctx->mch) { // normally destroyed in bulk_recv()
        diag_stats (ctx->mch);
        msgchan_destroy (ctx->mch);
    }
    flux_msg_decref (ctx->testmsg);
}

int bulk_init (struct bulk_test *ctx,
               flux_reactor_t *r,
               int count,
               bulk_direction_t dir)
{
    static char data[8192] = { 0 };
    flux_error_t error;

    if (!(ctx->testmsg = flux_request_encode_raw ("bulk", data, sizeof (data))))
        return -1;
    unique_interthread_uri (ctx->uri, sizeof (ctx->uri));
    if (!(ctx->mch = msgchan_create (r, ctx->uri, &error))) {
        diag ("%s", error.text);
        return -1;
    }
    if (dir == RECV_FROM_SUBPROC) {
        if (!(ctx->h_recv = flux_open (ctx->uri, 0)))
            return -1;
        if (!(ctx->h_send = flux_open (msgchan_get_uri (ctx->mch), 0)))
            return -1;
    }
    else {
        if (!(ctx->h_recv = flux_open (msgchan_get_uri (ctx->mch), 0)))
            return -1;
        if (!(ctx->h_send = flux_open (ctx->uri, 0)))
            return -1;
    }
    if (!(ctx->w_recv = flux_handle_watcher_create (r,
                                                    ctx->h_recv,
                                                    FLUX_POLLIN,
                                                    bulk_recv_cb,
                                                    ctx)))
        return -1;
    if (!(ctx->w_send = flux_handle_watcher_create (r,
                                                    ctx->h_send,
                                                    FLUX_POLLOUT,
                                                    bulk_send_cb,
                                                    ctx)))
        return -1;
    flux_watcher_start (ctx->w_send);
    flux_watcher_start (ctx->w_recv);
    ctx->count = count;
    return 0;
}

void test_bulk (flux_reactor_t *r, int count, bulk_direction_t dir)
{
    struct bulk_test ctx = { 0 };
    int rc;

    if (bulk_init (&ctx, r, count, dir) < 0) {
        BAIL_OUT ("bulk init failure: %s", strerror (errno));
        goto done;
    }
    rc = flux_reactor_run (r, 0);
    ok (rc == 0,
        "%s successfully transferred %d large messages",
        dir == RECV_FROM_SUBPROC ? "subproc" : "user",
        count);
done:
    bulk_fini (&ctx);
}

int main (int argc, char **argv)
{
    flux_reactor_t *r;

    plan (NO_PLAN);

    int start_fdcount = fdcount ();

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("could not create reactor");

    test_basic (r);
    test_bulk (r, 1000, SEND_TO_SUBPROC);
    test_bulk (r, 1000, RECV_FROM_SUBPROC);

    flux_reactor_destroy (r);

    int end_fdcount = fdcount ();

    ok (start_fdcount == end_fdcount,
        "no file descriptors leaked");
    if (start_fdcount != end_fdcount)
        diag ("test leaked %d file descriptors", end_fdcount - start_fdcount);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
