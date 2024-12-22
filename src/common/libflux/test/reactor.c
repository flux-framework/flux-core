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
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"

void watcher_is (flux_watcher_t *w,
                 bool exp_active,
                 bool exp_referenced,
                 const char *name,
                 const char *what)
{
    bool is_active = flux_watcher_is_active (w);
    bool is_referenced = flux_watcher_is_referenced (w);

    ok (is_active == exp_active && is_referenced == exp_referenced,
        "%s %sact%sref after %s",
        name,
        exp_active ? "+" : "-",
        exp_referenced ? "+" : "-",
        what);
    if (is_active != exp_active)
        diag ("%s is unexpectedly %sact", name, is_active ? "+" : "-");
    if (is_referenced != exp_referenced)
        diag ("%s is unexpectedly %sref", name, is_referenced ? "+" : "-");
}

/* Call this on newly created watcher to check start/stop/is_active and
 * ref/unref/is_referenced basics.
 */
void generic_watcher_check (flux_watcher_t *w, const char *name)
{
    /* ref/unref while inactive causes ev_ref/ev_unref to run
     * in the start/stop callbacks
     */
    watcher_is (w, false, true, name, "init");
    flux_watcher_unref (w);
    watcher_is (w, false, false, name, "unref");
    flux_watcher_start (w);
    watcher_is (w, true, false, name, "start");
    flux_watcher_stop (w);
    watcher_is (w, false, false, name, "stop");
    flux_watcher_ref (w);
    watcher_is (w, false, true, name, "ref");

    /* ref/unref while active causes ev_ref/ev_unref to run
     * in the ref/unref callbacks
     */
    flux_watcher_start (w);
    watcher_is (w, true, true, name, "start");
    flux_watcher_unref (w);
    watcher_is (w, true, false, name, "unref");
    flux_watcher_ref (w);
    watcher_is (w, true, true, name, "ref");
    flux_watcher_stop (w);
    watcher_is (w, false, true, name, "stop");
}

static const size_t fdwriter_bufsize = 10*1024*1024;

static void fdwriter (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        if ((n = write (fd, buf + count, fdwriter_bufsize - count)) < 0
            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: write failed: %s\n",
                     __FUNCTION__,
                     strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}
static void fdreader (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        if ((n = read (fd, buf + count, fdwriter_bufsize - count)) < 0
            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: read failed: %s\n",
                     __FUNCTION__,
                     strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void test_fd (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *r, *w;

#ifdef SOCK_NONBLOCK
    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
#else
    ok (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) == 0
	&& fd_set_nonblocking (fd[0]) >= 0
	&& fd_set_nonblocking (fd[1]) >= 0,
#endif
        "fd: successfully created non-blocking socketpair");
    r = flux_fd_watcher_create (reactor, fd[0], FLUX_POLLIN, fdreader, NULL);
    w = flux_fd_watcher_create (reactor, fd[1], FLUX_POLLOUT, fdwriter, NULL);
    ok (r != NULL && w != NULL,
        "fd: reader and writer created");
    generic_watcher_check (w, "fd");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "fd: reactor ran to completion after %lu bytes", fdwriter_bufsize);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);
    close (fd[0]);
    close (fd[1]);
}

static int repeat_countdown = 10;
static void repeat (flux_reactor_t *r,
                    flux_watcher_t *w,
                    int revents,
                    void *arg)
{
    repeat_countdown--;
    if (repeat_countdown == 0)
        flux_watcher_stop (w);
}

static int oneshot_runs = 0;
static int oneshot_errno = 0;
static void oneshot (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    oneshot_runs++;
    if (oneshot_errno != 0) {
        errno = oneshot_errno;
        flux_reactor_stop_error (r);
    }
}

static void test_timer (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    double elapsed, t0, t[] = { 0.001, 0.010, 0.050, 0.100, 0.200 };
    int i, rc;

    /* in case this test runs a while after last reactor run.
     */
    flux_reactor_now_update (reactor);

    errno = 0;
    ok (!flux_timer_watcher_create (reactor, -1, 0, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative timeout fails with EINVAL");
    ok (!flux_timer_watcher_create (reactor, 0, -1, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative repeat fails with EINVAL");
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating zero timeout oneshot works");
    generic_watcher_check (w, "timer");
    flux_watcher_start (w);
    oneshot_runs = 0;
    t0 = flux_reactor_now (reactor);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    elapsed = flux_reactor_now (reactor) - t0;
    ok (oneshot_runs == 1,
        "timer: oneshot was executed once (%.3fs)", elapsed);
    oneshot_runs = 0;
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    ok (oneshot_runs == 0,
        "timer: expired oneshot didn't run");

    errno = 0;
    oneshot_errno = ESRCH;
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) < 0 && errno == ESRCH,
        "general: reactor stop_error worked with errno passthru");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_timer_watcher_create (reactor, 0.001, 0.001, repeat, NULL))
        != NULL,
        "timer: creating 1ms timeout with 1ms repeat works");
    flux_watcher_start (w);
    repeat_countdown = 10;
    t0 = flux_reactor_now (reactor);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    elapsed = flux_reactor_now (reactor) - t0;
    ok (repeat_countdown == 0,
        "timer: repeat timer ran 10x and stopped itself");
    ok (elapsed >= 0.001*10,
        "timer: elapsed time is >= 10*1ms (%.3fs)", elapsed);
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    oneshot_errno = 0;
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating timer watcher works");
    for (i = 0; i < ARRAY_SIZE (t); i++) {
        flux_timer_watcher_reset (w, t[i], 0);
        flux_watcher_start (w);
        t0 = flux_reactor_now (reactor);
        oneshot_runs = 0;
        rc = flux_reactor_run (reactor, 0);
        elapsed = flux_reactor_now (reactor) - t0;
        ok (rc == 0 && oneshot_runs == 1 && elapsed >= t[i],
            "timer: reactor ran %.3fs oneshot at >= time (%.3fs)", t[i], elapsed);
    }
    flux_watcher_destroy (w);
}


/* A reactor callback that immediately stops reactor without error */
static bool do_stop_callback_ran = false;
static void do_stop_reactor (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    do_stop_callback_ran = true;
    flux_reactor_stop (r);
}

double time_now ()
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0) {
        fprintf (stderr, "clock_gettime: %s\n", strerror (errno));
        return -1.;
    }
    return ts.tv_sec + ts.tv_nsec/1.e9;
}

/* Periodic watcher "reschedule callback* */
static bool resched_called = false;
static double resched_cb (flux_watcher_t *w, double now, void *arg)
{
    flux_reactor_t *r = arg;
    ok (r != NULL, "resched callback called with proper arg");
    resched_called = true;
    return (now + .1);
}

static double resched_cb_negative (flux_watcher_t *w, double now, void *arg)
{
    return (now - 100.);
}

/*  These tests exercise most basic functionality of periodic watchers,
 *   but we're not able to fully test whether periodic watcher respects
 *   time jumps (as described in ev(7) man page) with these simple
 *   tests.
 */
static void test_periodic (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    errno = 0;
    oneshot_errno = 0;
    ok (!flux_periodic_watcher_create (reactor, -1, 0, NULL, oneshot, NULL)
        && errno == EINVAL,
        "periodic: creating negative offset fails with EINVAL");
    ok (!flux_periodic_watcher_create (reactor, 0, -1, NULL, oneshot, NULL)
        && errno == EINVAL,
        "periodic: creating negative interval fails with EINVAL");
    ok ((w = flux_periodic_watcher_create (reactor, 0, 0, NULL, oneshot, NULL))
        != NULL,
        "periodic: creating zero offset/interval works");
    generic_watcher_check (w, "periodic");
    flux_watcher_start (w);

    oneshot_runs = 0;
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor ran to completion");
    ok (oneshot_runs == 1,
        "periodic: oneshot was executed once");
    oneshot_runs = 0;
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    repeat_countdown = 5;
    ok ((w = flux_periodic_watcher_create (reactor,
                                           0.01,
                                           0.01,
                                           NULL,
                                           repeat,
                                           NULL)) != NULL,
        "periodic: creating 10ms interval works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor ran to completion");
    ok (repeat_countdown == 0, "repeat ran for expected number of times");
    oneshot_runs = 0;

    /* test reset */
    flux_periodic_watcher_reset (w, time_now () + 123., 0, NULL);
    /* Give 1s error range, time may march forward between reset and now */
    diag ("next wakeup = %.2f, now + offset = %.2f",
          flux_watcher_next_wakeup (w), time_now () + 123.);
    ok (fabs (flux_watcher_next_wakeup (w) - (time_now () + 123.)) <= .5,
        "flux_periodic_watcher_reset works");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_periodic_watcher_create (reactor,
                                           0,
                                           0,
                                           resched_cb,
                                           do_stop_reactor,
                                           reactor)) != NULL,
        "periodic: creating with resched callback works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) >= 0,
        "periodic: reactor ran to completion");
    ok (resched_called, "resched_cb was called");
    ok (do_stop_callback_ran, "stop reactor callback was run");
    oneshot_runs = 0;
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    do_stop_callback_ran = false;
    ok ((w = flux_periodic_watcher_create (reactor,
                                           0,
                                           0,
                                           resched_cb_negative,
                                           do_stop_reactor,
                                           reactor)) != NULL,
        "periodic: create watcher with misconfigured resched callback");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor stopped immediately");
    ok (do_stop_callback_ran == false, "periodic: callback did not run");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

}

static int idle_count = 0;
static void idle_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    if (++idle_count == 42)
        flux_watcher_stop (w);
}

static void test_idle (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    w = flux_idle_watcher_create (reactor, idle_cb, NULL);
    ok (w != NULL,
        "created idle watcher");
    generic_watcher_check (w, "idle");
    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");
    ok (idle_count == 42,
        "idle watcher ran until stopped");
    flux_watcher_destroy (w);
}

static int prepare_count = 0;
static void prepare_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    prepare_count++;
}

static int check_count = 0;
static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    check_count++;
}

static int prepchecktimer_count = 0;
static void prepchecktimer_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    if (++prepchecktimer_count == 8)
        flux_reactor_stop (r);
}

static void test_prepcheck (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *prep;
    flux_watcher_t *chk;

    w = flux_timer_watcher_create (reactor,
                                   0.01,
                                   0.01,
                                   prepchecktimer_cb,
                                   NULL);
    ok (w != NULL,
        "created timer watcher that fires every 0.01s");
    ok (!flux_watcher_is_active (w),
        "flux_watcher_is_active() returns false");
    flux_watcher_start (w);

    prep = flux_prepare_watcher_create (reactor, prepare_cb, NULL);
    ok (w != NULL,
        "created prepare watcher");
    generic_watcher_check (prep, "prep");
    flux_watcher_start (prep);

    chk = flux_check_watcher_create (reactor, check_cb, NULL);
    ok (w != NULL,
        "created check watcher");
    generic_watcher_check (chk, "check");
    flux_watcher_start (chk);

    ok (flux_reactor_run (reactor, 0) >= 0,
        "reactor ran successfully");
    ok (prepchecktimer_count == 8,
        "timer fired 8 times, then reactor was stopped");
    diag ("prep %d check %d timer %d",
          prepare_count,
          check_count,
          prepchecktimer_count);
    ok (prepare_count >= 8,
        "prepare watcher ran at least once per timer");
    ok (check_count >= 8,
        "check watcher ran at least once per timer");

    flux_watcher_destroy (w);
    flux_watcher_destroy (prep);
    flux_watcher_destroy (chk);
}

static int sigusr1_count = 0;
static void sigusr1_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    if (++sigusr1_count == 8)
        flux_reactor_stop (r);
}

static void sigidle_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    if (kill (getpid (), SIGUSR1) < 0)
        flux_reactor_stop_error (r);
}

static void test_signal (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *idle;

    w = flux_signal_watcher_create (reactor, SIGUSR1, sigusr1_cb, NULL);
    ok (w != NULL,
        "created signal watcher");
    generic_watcher_check (w, "signal");
    flux_watcher_start (w);

    idle = flux_idle_watcher_create (reactor, sigidle_cb, NULL);
    ok (idle != NULL,
        "created idle watcher");
    flux_watcher_start (idle);

    ok (flux_reactor_run (reactor, 0) >= 0,
        "reactor ran successfully");
    ok (sigusr1_count == 8,
        "signal watcher handled correct number of SIGUSR1's");

    flux_watcher_destroy (w);
    flux_watcher_destroy (idle);
}

static pid_t child_pid = -1;
static void child_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    int pid = flux_child_watcher_get_rpid (w);
    int rstatus = flux_child_watcher_get_rstatus (w);
    ok (pid == child_pid,
        "child watcher called with expected rpid");
    ok (WIFSIGNALED (rstatus) && WTERMSIG (rstatus) == SIGHUP,
        "child watcher called with expected rstatus");
    flux_watcher_stop (w);
}

static void test_child  (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_reactor_t *r;

    child_pid = fork ();
    if (child_pid == 0) {
        pause ();
        exit (0);
    }
    errno = 0;
    w = flux_child_watcher_create (reactor, child_pid, false, child_cb, NULL);
    ok (w == NULL && errno == EINVAL,
        "child watcher failed with EINVAL on non-SIGCHLD reactor");
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "created reactor with SIGCHLD flag");
    w = flux_child_watcher_create (r, child_pid, false, child_cb, NULL);
    ok (w != NULL,
        "created child watcher");
    generic_watcher_check (w, "signal");

    ok (kill (child_pid, SIGHUP) == 0,
        "sent child SIGHUP");
    flux_watcher_start (w);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran successfully");
    flux_watcher_destroy (w);
    flux_reactor_destroy (r);
}

struct stat_ctx {
    int fd;
    char *path;
    int stat_size;
    int stat_nlink;
    enum { STAT_APPEND, STAT_WAIT, STAT_UNLINK } state;
};
static void stat_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct stat_ctx *ctx = arg;
    struct stat new, old;
    flux_stat_watcher_get_rstat (w, &new, &old);
    if (new.st_nlink == 0) {
        diag ("%s: nlink: old: %d new %d", __FUNCTION__,
                old.st_nlink, new.st_nlink);
        ctx->stat_nlink++;
        flux_watcher_stop (w);
    } else {
        if (old.st_size != new.st_size) {
            diag ("%s: size: old=%ld new=%ld", __FUNCTION__,
                  (long)old.st_size, (long)new.st_size);
            ctx->stat_size++;
            ctx->state = STAT_UNLINK;
        }
    }
}

static void stattimer_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct stat_ctx *ctx = arg;
    if (ctx->state == STAT_APPEND) {
        if (write (ctx->fd, "hello\n", 6) < 0 || close (ctx->fd) < 0)
            flux_reactor_stop_error (r);
        ctx->state = STAT_WAIT;
    } else if (ctx->state == STAT_UNLINK) {
        if (unlink (ctx->path) < 0)
            flux_reactor_stop_error (r);
        flux_watcher_stop (w);
    }
}

static void test_stat (flux_reactor_t *reactor)
{
    flux_watcher_t *w, *tw;
    struct stat_ctx ctx = {0};
    const char *tmpdir = getenv ("TMPDIR");

    ctx.path = xasprintf ("%s/reactor-test.XXXXXX", tmpdir ? tmpdir : "/tmp");
    ctx.fd = mkstemp (ctx.path);
    ctx.state = STAT_APPEND;

    ok (ctx.fd >= 0,
        "created temporary file");
    w = flux_stat_watcher_create (reactor, ctx.path, 0., stat_cb, &ctx);
    ok (w != NULL,
        "created stat watcher");
    generic_watcher_check (w, "stat");
    flux_watcher_start (w);

    tw = flux_timer_watcher_create (reactor,
                                    0.01,
                                    0.01,
                                    stattimer_cb,
                                    &ctx);
    ok (tw != NULL,
        "created timer watcher");
    flux_watcher_start (tw);

    /* Make sure rstat accessor fails if passed the wrong watcher type.
     */
    errno = 0;
    ok (flux_stat_watcher_get_rstat (tw, NULL, NULL) < 0 && errno == EINVAL,
        "flux_stat_watcher_get_rstat fails with EINVAL on wrong watcher type");

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");

    ok (ctx.stat_size == 1,
        "stat watcher invoked once for size change");
    ok (ctx.stat_nlink == 1,
        "stat watcher invoked once for nlink set to zero");

    flux_watcher_destroy (w);
    flux_watcher_destroy (tw);
    free (ctx.path);
}

static int handle_counter = 0;
static void handle_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    handle_counter++;
    flux_watcher_unref (w);
}

void test_handle (flux_reactor_t *r)
{
    flux_t *h;
    flux_watcher_t *w;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    w = flux_handle_watcher_create (r, h, FLUX_POLLIN, handle_cb, NULL);
    ok (w != NULL,
        "flux_handle_watcher_create works");
    generic_watcher_check (w, "handle");
    flux_watcher_start (w);

    flux_msg_t *msg;
    if (!(msg = flux_request_encode ("foo", "bar")))
        BAIL_OUT ("could not encode message");
    if (flux_send (h, msg, 0) < 0)
        BAIL_OUT ("could not send message");
    flux_msg_destroy (msg);

    ok (flux_reactor_run (r, 0) == 0,
        "flux_reactor_run ran");
    ok (handle_counter == 1,
        "watcher ran once");

    flux_watcher_destroy (w);
    flux_close (h);
}

static void unref_idle_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    int *count = arg;
    (*count)++;

    if (*count >= 16)
        flux_reactor_stop_error (r);
}

static void unref_idle2_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    int *count = arg;
    (*count)++;

    if (flux_watcher_is_referenced (w)) {
        diag ("calling flux_watcher_unref on count=%d", *count);
        flux_watcher_unref (w); // calls ev_unref()
    }
    else {
        diag ("calling flux_watcher_ref on count=%d", *count);
        flux_watcher_ref (w); // calls ev_ref()
    }
}

static void test_unref (flux_reactor_t *r)
{
    flux_watcher_t *w;
    flux_watcher_t *w2;
    int count;

    ok (flux_reactor_run (r, 0) == 0,
        "flux_reactor_run with no watchers returned immediately");

    if (!(w = flux_idle_watcher_create (r, unref_idle_cb, &count)))
        BAIL_OUT ("flux_idle_watcher_create failed");
    if (!(w2 = flux_idle_watcher_create (r, unref_idle2_cb, &count)))
        BAIL_OUT ("flux_idle_watcher_create failed");

    /* Tests with unref_idle_cb()
     * Show that ref/unref as expected with watcher inactive.
     */

    flux_watcher_start (w);
    count = 0;
    ok (flux_reactor_run (r, 0) < 0 && count == 16,
        "flux_reactor_run with one watcher stopped after 16 iterations");
    flux_watcher_stop (w);
    ok (flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced returns true after stop");

    flux_watcher_unref (w);
    flux_watcher_start (w); // calls ev_unref()
    ok (!flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced returns false after unref/start");
    count = 0;
    ok (flux_reactor_run (r, 0) == 0 && count == 1,
        "flux_reactor_run with one unref watcher returned after 1 iteration");
    flux_watcher_stop (w); // calls ev_ref()

    flux_watcher_ref (w);
    flux_watcher_start (w);
    ok (flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced returns true after ref/start");
    count = 0;
    ok (flux_reactor_run (r, 0) < 0 && count == 16,
        "flux_reactor_run with one ref watcher stopped after 16 iterations");
    flux_watcher_stop (w);
    ok (flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced returns true after reactor run");

    flux_watcher_destroy (w);

    /* Tests with unref_idle2_cb()
     * Show that ref/unref works as expected from watcher callback
     */

    flux_watcher_start (w2);

    ok (flux_watcher_is_referenced (w2),
        "flux_watcher_is_referenced returns true");
    count = 0;
    ok (flux_reactor_run (r, 0) == 0 && count == 1,
        "flux_reactor_run with one ref watcher returned after 1 iteration");
    ok (!flux_watcher_is_referenced (w2),
        "flux_watcher_is_referenced returns false after reactor run");

    count = 0;
    ok (flux_reactor_run (r, 0) == 0 && count == 2,
        "flux_reactor_run with one unref watcher returned after 2 iterations");
    ok (!flux_watcher_is_referenced (w2),
        "flux_watcher_is_referenced returns false");

    flux_watcher_destroy (w2);
}

static void reactor_destroy_early (void)
{
    flux_reactor_t *r;
    flux_watcher_t *w;

    if (!(r = flux_reactor_create (0)))
        exit (1);
    if (!(w = flux_idle_watcher_create (r, NULL, NULL)))
        exit (1);
    flux_watcher_start (w);
    flux_reactor_destroy (r);
    flux_watcher_destroy (w);
}

static void test_reactor_flags (flux_reactor_t *r)
{
    errno = 0;
    ok (flux_reactor_run (r, 0xffff) < 0 && errno == EINVAL,
        "flux_reactor_run flags=0xffff fails with EINVAL");

    errno = 0;
    ok (flux_reactor_create (0xffff) == NULL && errno == EINVAL,
        "flux_reactor_create flags=0xffff fails with EINVAL");
}

static char cblist[6] = {0};
static int cblist_index = 0;
static flux_watcher_t *priority_prep = NULL;
static flux_watcher_t *priority_idle = NULL;

static void priority_prep_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    flux_watcher_start (priority_idle);
}

static void priority_check_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    char *s = arg;
    /* stick the char name of this watcher into the array, we'll
     * compare later
     */
    cblist[cblist_index++] = s[0];
    if (cblist_index >= 5) {
        flux_watcher_stop (priority_prep);
        flux_watcher_stop (priority_idle);
    }
    flux_watcher_stop (w);
}

static void test_priority (flux_reactor_t *r)
{
    flux_watcher_t *a, *b, *c, *d, *e;
    priority_prep = flux_prepare_watcher_create (r, priority_prep_cb, NULL);
    ok (priority_prep != NULL,
        "prep watcher create worked");
    priority_idle = flux_idle_watcher_create (r, NULL, NULL);
    ok (priority_idle != NULL,
        "idle watcher create worked");
    a = flux_check_watcher_create (r, priority_check_cb, "A");
    b = flux_check_watcher_create (r, priority_check_cb, "B");
    c = flux_check_watcher_create (r, priority_check_cb, "C");
    d = flux_check_watcher_create (r, priority_check_cb, "D");
    e = flux_check_watcher_create (r, priority_check_cb, "E");
    ok (a != NULL && b != NULL && c != NULL && d != NULL && e != NULL,
        "check watcher create worked");
    // Don't set priority of 'a', it'll be default
    flux_watcher_set_priority (b, -2);
    flux_watcher_set_priority (c, 1);
    flux_watcher_set_priority (d, 2);
    flux_watcher_set_priority (e, -1);
    flux_watcher_start (a);
    flux_watcher_start (b);
    flux_watcher_start (c);
    flux_watcher_start (d);
    flux_watcher_start (e);
    flux_watcher_start (priority_prep);
    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran to completion");
    /* given priorities, callbacks should be called in the following order
     * DCAEB
     */
    ok (memcmp (cblist, "DCAEB", 5) == 0,
        "callbacks called in the correct order");
    flux_watcher_destroy (a);
    flux_watcher_destroy (b);
    flux_watcher_destroy (c);
    flux_watcher_destroy (d);
    flux_watcher_destroy (e);
    flux_watcher_destroy (priority_prep);
    flux_watcher_destroy (priority_idle);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran to completion (no watchers)");

    ok (!flux_watcher_is_active (NULL),
        "flux_watcher_is_active (NULL) returns false");

    test_timer (reactor);
    test_periodic (reactor);
    test_fd (reactor);
    test_idle (reactor);
    test_prepcheck (reactor);
    test_signal (reactor);
    test_child (reactor);
    test_stat (reactor);
    test_handle (reactor);
    test_unref (reactor);
    test_reactor_flags (reactor);
    test_priority (reactor);

    flux_reactor_destroy (reactor);

    lives_ok ({ reactor_destroy_early ();},
        "destroying reactor then watcher doesn't segfault");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

