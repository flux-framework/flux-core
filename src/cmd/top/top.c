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
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>

#include "src/common/libutil/uri.h"
#include "top.h"

static const double job_activity_rate_limit = 2;

__attribute__ ((noreturn)) void fatal (int errnum, const char *fmt, ...)
{
    va_list ap;
    char buf[128];

    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    endwin ();
    fprintf (stderr,
             "flux-top: %s%s%s\n",
             buf,
             errnum == 0 ? "" : ": ",
             errnum == 0 ? "" : strerror (errnum));
    exit (1);
}

/* When connection is lost to Flux, this function is called before
 * errors propagate to higher level functions (like RPCs), so it
 * is an opportunity to consolidate error handling for that case.
 * N.B. ssh:// connections do not always propagate fatal errors as expected.
 * N.B. the msg argument would appear to be mostly useless for our purposes.
 */
static void flux_fatal (const char *msg, void *arg)
{
    fatal (0, "lost connection to Flux");
}

static void heartbeat_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct top *top = arg;
    summary_pane_heartbeat (top->summary_pane);
    summary_pane_draw (top->summary_pane);
    joblist_pane_draw (top->joblist_pane);
}

static void jobtimer_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct top *top = arg;

    summary_pane_query (top->summary_pane);
    joblist_pane_query (top->joblist_pane);
    top->jobtimer_running = false;
}

/* After some job state activity, and after a rate-limited delay,
 * trigger queries for info in the two panes.
 */
static void job_state_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct top *top = arg;

    if (!top->jobtimer_running) {
        flux_timer_watcher_reset (top->jobtimer, job_activity_rate_limit, 0.);
        flux_watcher_start (top->jobtimer);
        top->jobtimer_running = true;
    }
}

void refresh_cb (flux_reactor_t *r,
                 flux_watcher_t *w,
                 int revents,
                 void *arg)
{
    struct top *top = arg;

    summary_pane_refresh (top->summary_pane);
    joblist_pane_refresh (top->joblist_pane);
    doupdate ();
}

/* Get handle to Flux instance to be monitored.
 * If id = FLUX_JOBID_ANY, merely call flux_open().
 * Otherwise, fetch remote-uri from job and open that.
 */
static flux_t *open_flux_instance (const char *target)
{
    flux_t *h;
    flux_future_t *f = NULL;
    char *uri = NULL;

    if (target && !(uri = uri_resolve (target)))
        fatal (0, "failed to resolve target %s to a Flux URI", target);
    if (!(h = flux_open (uri, 0)))
        fatal (errno, "error connecting to Flux");
    free (uri);
    flux_future_destroy (f);
    return h;
}

/* Initialize 'stdscr' and register colors.
 * N.B. this program does not use stdscr, but it does use getch(), which
 * implicitly refreshes stdscr.  Therefore, make sure that stdscr is synced
 * with its internal buffer by calling refresh() below to prevent unwanted
 * screen updates on the first keypress.
 */
static void initialize_curses (void)
{
    initscr ();
    curs_set (0); // make cursor disappear

    use_default_colors ();
    start_color ();
    init_pair (TOP_COLOR_YELLOW, COLOR_YELLOW, -1);
    init_pair (TOP_COLOR_RED, COLOR_RED, -1);

    clear ();
    refresh ();
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT, "job-state", job_state_cb, 0 },
    { FLUX_MSGTYPE_EVENT, "heartbeat.pulse", heartbeat_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static struct optparse_option cmdopts[] = {
    { .name = "test-exit", .has_arg = 0, .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Exit after screen initialization, for testing",
    },
    OPTPARSE_TABLE_END,
};

static const char *usage_msg = "[OPTIONS] [TARGET]";

int main (int argc, char *argv[])
{
    int optindex;
    struct top top;
    int reactor_flags = 0;
    const char *target = NULL;

    memset (&top, 0, sizeof (top));
    top.id = FLUX_JOBID_ANY;

    setlocale (LC_ALL, "");

    if (!(top.opts = optparse_create ("flux-top"))
        || optparse_add_option_table (top.opts, cmdopts) != OPTPARSE_SUCCESS
        || optparse_set (top.opts,
                         OPTPARSE_USAGE,
                         usage_msg) != OPTPARSE_SUCCESS)
        fatal (0, "error setting up option parsing");

    if ((optindex = optparse_parse_args (top.opts, argc, argv)) < 0)
        exit (1);
    if (optindex < argc)
        target = argv[optindex++];
    if (optindex != argc) {
        optparse_print_usage (top.opts);
        exit (1);
    }
    top.h = open_flux_instance (target);
    flux_fatal_set (top.h, flux_fatal, &top);
    if (!(top.refresh = flux_prepare_watcher_create (flux_get_reactor (top.h),
                                                     refresh_cb,
                                                     &top))
        || !(top.jobtimer = flux_timer_watcher_create (flux_get_reactor (top.h),
                                                       0.,
                                                       0.,
                                                       jobtimer_cb,
                                                       &top)))
        fatal (errno, "could not create timers");
    flux_watcher_start (top.refresh);

    if (flux_msg_handler_addvec (top.h, htab, &top, &top.handlers) < 0)
        fatal (errno, "error registering message handlers");
    if (flux_event_subscribe (top.h, "job-state") < 0
        || flux_event_subscribe (top.h, "heartbeat.pulse") < 0)
        fatal (errno, "error subscribing to events");

    if (!isatty (STDIN_FILENO))
        fatal (0, "stdin is not a terminal");
    initialize_curses ();
    top.keys = keys_create (&top);
    top.summary_pane = summary_pane_create (&top);
    top.joblist_pane = joblist_pane_create (&top);

    if (optparse_hasopt (top.opts, "test-exit"))
        reactor_flags |= FLUX_REACTOR_ONCE;
    if (flux_reactor_run (flux_get_reactor (top.h), reactor_flags) < 0)
        fatal (errno, "reactor loop unexpectedly terminated");

    flux_watcher_destroy (top.refresh);
    flux_watcher_destroy (top.jobtimer);
    flux_msg_handler_delvec (top.handlers);
    joblist_pane_destroy (top.joblist_pane);
    summary_pane_destroy (top.summary_pane);
    keys_destroy (top.keys);
    endwin ();
    flux_close (top.h);
    optparse_destroy (top.opts);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
