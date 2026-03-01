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
#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"
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
 */
static int comms_error (flux_t *h, void *arg)
{
    fatal (0, "lost connection to Flux: %s", strerror (errno));
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
    summary_pane_query (top->summary_pane);
}

static void jobtimer_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct top *top = arg;

    joblist_pane_query (top->joblist_pane);
    top->jobtimer_running = false;
}

/* After some job stats activity, and after a rate-limited delay,
 * trigger queries for info in the two panes.
 */
static void stats_continuation (flux_future_t *f, void *arg)
{
    struct top *top = arg;
    if (!top->jobtimer_running) {
        flux_timer_watcher_reset (top->jobtimer, job_activity_rate_limit, 0.);
        flux_watcher_start (top->jobtimer);
        top->jobtimer_running = true;
    }
    summary_pane_jobstats (top->summary_pane, f);
    flux_future_reset (f);
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
static flux_t *open_flux_instance (const char *target, flux_error_t *errp)
{
    flux_t *h;
    flux_error_t error;
    flux_future_t *f = NULL;
    char *uri = NULL;

    if (target && !(uri = uri_resolve (target, &error))) {
        errprintf (errp,
                   "%s\n%s",
                   "failed to resolve target to a Flux URI",
                   error.text);
        return NULL;
    }
    if (!(h = flux_open_ex (uri, 0, &error)))
        errprintf (errp,
                   "error connecting to Flux: %s\n%s",
                   strerror (errno),
                   error.text);
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
static void initialize_curses (int color)
{
    char *cap;
    initscr ();
    if (!(cap = tigetstr ("cup")) || cap == (char *) -1)
        fatal (0, "terminal does not support required capabilities");

    curs_set (0); // make cursor disappear

    use_default_colors ();
    start_color ();
    if (color) {
        init_pair (TOP_COLOR_YELLOW, COLOR_YELLOW, -1);
        init_pair (TOP_COLOR_RED, COLOR_RED, -1);
        init_pair (TOP_COLOR_GREEN, COLOR_GREEN, -1);
        init_pair (TOP_COLOR_BLUE, COLOR_BLUE, -1);
        init_pair (TOP_COLOR_BLUE_HIGHLIGHT, COLOR_BLACK, COLOR_BLUE);
    }
    else {
        init_pair (TOP_COLOR_YELLOW, -1, -1);
        init_pair (TOP_COLOR_RED, -1, -1);
        init_pair (TOP_COLOR_GREEN, -1, -1);
        init_pair (TOP_COLOR_BLUE, -1, -1);
        init_pair (TOP_COLOR_BLUE_HIGHLIGHT, -1, -1);
    }
    clear ();
    refresh ();
}

int top_run (struct top *top, int reactor_flags)
{
    /*  Force curses to redraw screen in case we're calling top recursively.
     *  (unsure why refresh() doesn't have same effect as wrefresh (curscr))
     */
    wrefresh (curscr);
    return flux_reactor_run (flux_get_reactor (top->h), reactor_flags);
}

void test_exit_check (struct top *top)
{
    /* 3 exit counts for
     * - joblist output
     * - summary stats output
     * - summary resource output
     */
    if (top->test_exit && ++top->test_exit_count == 3)
        flux_reactor_stop (flux_get_reactor (top->h));
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT, "heartbeat.pulse", heartbeat_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void top_destroy (struct top *top)
{
    if (top) {
        flux_watcher_destroy (top->refresh);
        flux_watcher_destroy (top->jobtimer);
        flux_future_destroy (top->f_stats);
        flux_msg_handler_delvec (top->handlers);
        joblist_pane_destroy (top->joblist_pane);
        summary_pane_destroy (top->summary_pane);
        keys_destroy (top->keys);
        queues_destroy (top->queues);
        json_decref (top->flux_config);
        if (top->testf)
            fclose (top->testf);
        flux_close (top->h);
        free (top->title);
        free (top);
    }
}

static flux_jobid_t get_jobid (flux_t *h)
{
    const char *s;
    flux_jobid_t jobid;

    if (!(s = flux_attr_get (h, "jobid")))
        return FLUX_JOBID_ANY;
    if (flux_job_id_parse (s, &jobid) < 0)
        fatal (errno, "error parsing value of jobid attribute: %s", s);
    return jobid;
}

static char * build_title (struct top *top, const char *title)
{
    if (!title)
        title = top->id == FLUX_JOBID_ANY ? "" : idf58 (top->id);
    return strdup (title);
}

static void get_config (struct top *top)
{
    flux_future_t *f;
    json_t *o;

    if (!(f = flux_rpc (top->h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "o", &o) < 0)
        fatal (errno, "Error fetching flux config");

    top->flux_config = json_incref (o);
    flux_future_destroy (f);
}

struct top *top_create (const char *uri,
                        const char *title,
                        const char *queue,
                        flux_error_t *errp)
{
    struct top *top = calloc (1, sizeof (*top));

    if (!top || !(top->h = open_flux_instance (uri, errp)))
        goto fail;

    top->id = get_jobid (top->h);
    if (!(top->title = build_title (top, title)))
        goto fail;

    get_config (top);

    if (!(top->queues = queues_create (top->flux_config)))
        goto fail;

    /* setup / configure queue before calls to joblist_pane_create() and
     * summary_pane_create() below */
    if (queue)
        queues_set_queue (top->queues, queue);

    flux_comms_error_set (top->h, comms_error, &top);
    top->refresh = flux_prepare_watcher_create (flux_get_reactor (top->h),
                                                refresh_cb,
                                                top);
    top->jobtimer = flux_timer_watcher_create (flux_get_reactor (top->h),
                                               0.,
                                               0.,
                                               jobtimer_cb,
                                               top);
    if (!top->refresh || !top->jobtimer)
        goto fail;
    flux_watcher_start (top->refresh);

    if (flux_msg_handler_addvec (top->h, htab, top, &top->handlers) < 0)
        goto fail;
    if (flux_event_subscribe (top->h, "heartbeat.pulse") < 0)
        fatal (errno, "error subscribing to events");
     if (!(top->f_stats = flux_rpc (top->h,
                                    "job-list.job-stats",
                                    "{}",
                                    0,
                                    FLUX_RPC_STREAMING))
            || flux_future_then (top->f_stats,
                                 -1,
                                 stats_continuation,
                                 top) < 0)
         fatal (errno, "error making streaming job-stats request");

#if ASSUME_BROKEN_LOCALE
    top->f_char = "f";
#else
    if (getenv ("FLUX_F58_FORCE_ASCII"))
        top->f_char = "f";
    else
        top->f_char = "ƒ";
#endif /* ASSUME_BROKEN_LOCALE */

    top->keys = keys_create (top);
    top->summary_pane = summary_pane_create (top);
    top->joblist_pane = joblist_pane_create (top);
    return top;
fail:
    top_destroy (top);
    return NULL;
}

static struct optparse_option cmdopts[] = {
    { .name = "test-exit", .has_arg = 0, .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Exit after screen initialization, for testing",
    },
    { .name = "test-exit-dump", .has_arg = 1, .arginfo = "FILE",
      .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Dump joblist/summary data to file for testing",
    },
    { .name = "color", .has_arg = 2, .arginfo = "WHEN",
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)." },
    { .name = "queue", .key = 'q', .has_arg = 1, .arginfo = "NAME",
      .usage = "Limit to jobs belonging to a specific queue",
    },
    OPTPARSE_TABLE_END,
};

static const char *usage_msg = "[OPTIONS] [TARGET]";

int main (int argc, char *argv[])
{
    int optindex;
    struct top *top;
    int reactor_flags = 0;
    const char *target = NULL;
    optparse_t *opts;
    flux_error_t error;

    setlocale (LC_ALL, "");

    if (!(opts = optparse_create ("flux-top"))
        || optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS
        || optparse_set (opts,
                         OPTPARSE_USAGE,
                         usage_msg) != OPTPARSE_SUCCESS)
        fatal (0, "error setting up option parsing");

    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);
    if (optindex < argc)
        target = argv[optindex++];
    if (optindex != argc) {
        optparse_print_usage (opts);
        exit (1);
    }
    if (!isatty (STDIN_FILENO))
        fatal (0, "stdin is not a terminal");
    initialize_curses (optparse_get_color (opts, "color"));

    if (!(top = top_create (target,
                            NULL,
                            optparse_get_str (opts, "queue", NULL),
                            &error)))
        fatal (0, "%s", error.text);
    /* top_create() call above will call initial "draw" routines.  We
     * do not want those draws to output anything during testing with
     * the --test-exit-dump option.  Thus we handle --test-exit and
     * --test-exit-dump setup after the top_create() call.
     */
    if (optparse_hasopt (opts, "test-exit")) {
        const char *file;
        top->test_exit = 1;
        if ((file = optparse_get_str (opts, "test-exit-dump", NULL))) {
            mode_t umask_orig = umask (022);
            if (!(top->testf = fopen (file, "w+")))
                fatal (errno, "failed to open test dump file");
            umask (umask_orig);
        }
    }
    if (top_run (top, reactor_flags) < 0)
        fatal (errno, "reactor loop unexpectedly terminated");

    if (top->test_exit) {
        curs_set (1); // restore cursor
        reset_shell_mode ();
    }
    else
        endwin ();
    top_destroy (top);
    optparse_destroy (opts);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
