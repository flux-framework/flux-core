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
#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

void send_fake_heartbeat (flux_t *h, int seq)
{
    flux_msg_t *msg;

    if (!(msg = flux_event_encode ("heartbeat.pulse", NULL))
            || flux_msg_set_seq (msg, seq) < 0
            || flux_send (h, msg, 0) < 0)
        BAIL_OUT ("failed to send fake heartbeat");
    flux_msg_destroy (msg);
    diag ("sent heartbeat");
}

/* N.B. It's not advisable to use flux_sync_create() in this manner as
 * the old event messages accumulate in the handle queue.  (See "special note"
 * in src/common/libflux/sync.c).  However it should _work_ to be consistent
 * with expected future semantics.
 */
void test_non_reactive_loop (void)
{
    flux_t *h;
    flux_future_t *f;
    int rc;
    int i;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    f = flux_sync_create (h, 0.);
    ok (f != NULL,
        "flux_sync_create works");

    errno = 0;
    ok (flux_future_wait_for (f, 0.1) < 0,
        "flux_future_wait_for timed out waiting for (not sent) heartbeat");
    flux_future_reset (f); // not needed on timeout but if test fails...

    for (i = 0; i < 4; i++) {
        send_fake_heartbeat (h, i);
        rc = flux_future_wait_for (f, 10.);
        ok (rc == 0,
            "flux_future_wait_for (%d) success", i);
        if (rc < 0)
            diag ("flux_future_wait_for (%d): %s", i, strerror (errno));
        ok (flux_future_get (f, NULL) == 0,
            "flux_future_get (%d) success", i);
        flux_future_reset (f);
    }

    errno = 0;
    rc = flux_future_wait_for (f, 0.1);
    ok (rc < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for timed out waiting for (not sent) heartbeat");
    if (rc == 0 || errno != ETIMEDOUT)
        diag ("flux_future_wait_for: %s", rc==0 ? "success" : strerror (errno));

    flux_future_destroy (f);
    flux_close (h);
}

struct heartbeat_ctx {
    int seq;
    flux_t *h;
};

void heartbeat_timer (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct heartbeat_ctx *ctx = arg;

    send_fake_heartbeat (ctx->h, ctx->seq++);
}

void continuation (flux_future_t *f, void *arg)
{
    int *count = arg;

    diag ("continuation %d", *count);
    if (--(*count) == 0)
        flux_reactor_stop (flux_future_get_reactor (f));
    else
        flux_future_reset (f);
}

void test_sync_reactive (double heartrate, double min, double max)
{
    flux_t *h = flux_open ("loop://", 0);
    struct heartbeat_ctx ctx = { .h = h, .seq = 0 };
    flux_reactor_t *r;
    flux_watcher_t *timer;
    flux_future_t *f;
    int count;

    if (!h)
        BAIL_OUT ("could not create loop handle");
    if (!(r = flux_get_reactor (h)))
        BAIL_OUT ("flux_get_reactor failed on loopback handle");

    if (!(timer = flux_timer_watcher_create (r,
                                             0.,
                                             heartrate,
                                             heartbeat_timer,
                                             &ctx)))
        BAIL_OUT ("could not create timer watcher");
    flux_watcher_start (timer);

    f = flux_sync_create (h, min);
    ok (f != NULL,
        "flux_sync_create works");
    ok (flux_future_then (f, max, continuation, &count) == 0,
        "flux_future_then heartrate=%.2f min=%.2f max=max", heartrate, min,max);
    count = 4;
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run returned success");
    ok (count == 0,
        "sync continuation ran the expected number of times");

    flux_future_destroy (f);
    flux_watcher_destroy (timer);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errno = 0;
    ok (flux_sync_create (NULL, 0.) == NULL && errno == EINVAL,
        "flux_sync_create h=NULL fails with EINVAL");

    test_non_reactive_loop ();
    test_sync_reactive (0.01, 0.,   5.);    // driven by heartbeat
    test_sync_reactive (0.01, 0.1,  5.);    //   same, but skip some heartbeats
    test_sync_reactive (5.,   0,    0.01);  // driven by 'max' timeout

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

