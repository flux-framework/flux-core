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
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>
#include <math.h>

#include "src/common/libutil/fsd.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

#include "top.h"

static const struct dimension win_dim = { 0, 0, 80, 6 };
static const struct dimension level_dim = { 0, 0, 2, 1 };
static const struct dimension title_dim = { 6, 0, 73, 1 };
static const struct dimension timeleft_dim = { 70, 0, 10, 1 };
static const struct dimension resource_dim = { 4, 1, 36, 3 };
static const struct dimension heart_dim = { 77, 3, 1, 1 };
static const struct dimension stats_dim = { 60, 1, 15, 3 };
static const struct dimension info_dim = { 1, 5, 78, 1 };

static const double heartblink_duration = 0.5;

struct resource_count {
    int total;
    int down;
    int used;
};

struct stats {
    int depend;
    int priority;
    int sched;
    int run;
    int cleanup;
    int inactive;
    int successful;
    int failed;
    int canceled;
    int timeout;
    int total;
};

struct summary_pane {
    struct top *top;
    WINDOW *win;
    int instance_level;
    int instance_size;
    double starttime;
    uid_t owner;
    bool show_details;
    const char *instance_version;
    double expiration;
    struct stats stats;
    struct resource_count node;
    struct resource_count core;
    struct resource_count gpu;
    flux_watcher_t *heartblink;
    bool heart_visible;
    flux_jobid_t current;
    json_t *jobs;
    flux_future_t *f_resource;
};

static void draw_timeleft (struct summary_pane *sum)
{
    double now = flux_reactor_now (flux_get_reactor (sum->top->h));
    double timeleft = sum->expiration - now;
    char buf[32] = "";

    if (timeleft > 0)
        fsd_format_duration_ex (buf, sizeof (buf), timeleft, 2);

    mvwprintw (sum->win,
               timeleft_dim.y_begin,
               timeleft_dim.x_begin,
               "%*s%s",
               timeleft_dim.x_length - 2,
               buf,
               timeleft > 0 ? "⌚" : "∞");
}

static void draw_f (struct summary_pane *sum)
{
    wattron (sum->win, COLOR_PAIR (TOP_COLOR_YELLOW));
    mvwprintw (sum->win,
               level_dim.y_begin,
               level_dim.x_begin,
               "%s",
               sum->top->f_char);
    wattroff (sum->win, COLOR_PAIR (TOP_COLOR_YELLOW));
}

static void draw_title (struct summary_pane *sum)
{
    int len = strlen (sum->top->title);
    int begin = title_dim.x_begin + (title_dim.x_length - len)/2;
    int start = 0;
    char *dots = "";

    if (len > title_dim.x_length) {
        dots = "…";
        begin = title_dim.x_begin;
        start = (len - title_dim.x_length) + strlen (dots) - 1;
    }
    wattron (sum->win, COLOR_PAIR(TOP_COLOR_BLUE) | A_BOLD);
    mvwprintw (sum->win,
               title_dim.y_begin,
                begin,
               "%s%s",
                dots,
                sum->top->title + start);
    wattroff (sum->win, COLOR_PAIR(TOP_COLOR_BLUE) | A_BOLD);
}

static void draw_stats (struct summary_pane *sum)
{
    mvwprintw (sum->win,
               stats_dim.y_begin,
               stats_dim.x_begin,
               "%*d pending",
               stats_dim.x_length - 10,
               sum->stats.depend + sum->stats.priority + sum->stats.sched);
    mvwprintw (sum->win,
               stats_dim.y_begin + 1,
               stats_dim.x_begin,
               "%*d running",
               stats_dim.x_length - 10,
               sum->stats.run + sum->stats.cleanup);

    if (sum->top->testf) {
        fprintf (sum->top->testf,
                 "%d pending\n",
                 sum->stats.depend + sum->stats.priority + sum->stats.sched);
        fprintf (sum->top->testf,
                 "%d running\n",
                 sum->stats.run + sum->stats.cleanup);
    }

    if (sum->show_details) {
        /* flux-top reports the total number of unsuccessful jobs in
         * the 'failed' display, not just the count of jobs that ran
         * to completion with nonzero exit code
         */
        int failed = sum->stats.failed + sum->stats.timeout + sum->stats.canceled;
        int complete = sum->stats.successful;

        if (complete)
            wattron (sum->win, COLOR_PAIR(TOP_COLOR_GREEN) | A_BOLD);
        mvwprintw (sum->win,
                   stats_dim.y_begin + 2,
                   stats_dim.x_begin - 18,
                   "%6d",
                   complete);
        if (complete)
            wattroff (sum->win, COLOR_PAIR(TOP_COLOR_GREEN) | A_BOLD);
        mvwprintw (sum->win,
                   stats_dim.y_begin + 2,
                   stats_dim.x_begin - 12,
                   " complete, ");
        if (failed)
            wattron (sum->win, COLOR_PAIR(TOP_COLOR_RED) | A_BOLD);
        mvwprintw (sum->win,
                   stats_dim.y_begin + 2,
                   stats_dim.x_begin - 1,
                   "%6d",
                   failed);
        if (failed)
            wattroff (sum->win, COLOR_PAIR(TOP_COLOR_RED) | A_BOLD);
        mvwprintw (sum->win,
                   stats_dim.y_begin + 2,
                   stats_dim.x_begin + 5,
                   " failed");

        if (sum->top->testf) {
            fprintf (sum->top->testf,
                     "%d complete\n",
                     complete);
            fprintf (sum->top->testf,
                     "%d failed\n",
                     failed);
        }
    }
    else {
        mvwprintw (sum->win,
                   stats_dim.y_begin + 2,
                   stats_dim.x_begin,
                   "%*d inactive",
                   stats_dim.x_length - 10,
                   sum->stats.inactive);
        if (sum->top->testf) {
            fprintf (sum->top->testf,
                     "%d inactive\n",
                     sum->stats.inactive);
        }
    }
}

/* Create a little graph like this that fits in bufsz:
 *     name [||||||||||        |||32/128]
 * "used" grows from the left in yellow; "down" grows from the right in red.
 * Fraction is used/total.
 */
static void draw_bargraph (struct summary_pane *sum, int y, int x, int x_length,
                           const char *name, struct resource_count res)
{
    char prefix[16];
    char suffix[16];

    if (x_length > 80)
        x_length = 80;
    if (res.used > res.total)
        res.used = res.total;

    snprintf (prefix, sizeof (prefix), "%5s [", name);
    snprintf (suffix, sizeof (suffix), "%d/%d]", res.used, res.total);

    int slots = x_length - strlen (prefix) - strlen (suffix) - 1;
    mvwprintw (sum->win,
               y,
               x,
               "%s%*s%s",
               prefix,
               slots, "",
               suffix);
    /* Graph used */
    wattron (sum->win, COLOR_PAIR (TOP_COLOR_YELLOW));
    for (int i = 0; i < ceil (((double)res.used / res.total) * slots); i++)
        mvwaddch (sum->win, y, x + strlen (prefix) + i, '|');
    wattroff (sum->win, COLOR_PAIR (TOP_COLOR_YELLOW));

    /* Graph down */
    wattron (sum->win, COLOR_PAIR (TOP_COLOR_RED));
    for (int i = slots - 1;
         i >= slots - ceil (((double)res.down / res.total) * slots); i--) {
        mvwaddch (sum->win, y, x + strlen (prefix) + i, '|');
    }
    wattroff (sum->win, COLOR_PAIR (TOP_COLOR_RED));

    if (sum->top->testf)
        fprintf (sum->top->testf,
                 "%s %d/%d\n",
                 name,
                 res.used,
                 res.total);
}

static void draw_resource (struct summary_pane *sum)
{
    draw_bargraph (sum,
                   resource_dim.y_begin,
                   resource_dim.x_begin,
                   resource_dim.x_length,
                   "nodes",
                   sum->node);
    draw_bargraph (sum,
                   resource_dim.y_begin + 1,
                   resource_dim.x_begin,
                   resource_dim.x_length,
                   "cores",
                   sum->core);
    draw_bargraph (sum,
                   resource_dim.y_begin + 2,
                   resource_dim.x_begin,
                   resource_dim.x_length,
                   "gpus",
                   sum->gpu);
}

static void draw_heartbeat (struct summary_pane *sum)
{
    mvwprintw (sum->win,
               heart_dim.y_begin,
               heart_dim.x_begin,
               "%s",
               sum->heart_visible ? "♡" : " ");
}

static void draw_info (struct summary_pane *sum)
{
    double now = flux_reactor_now (flux_get_reactor (sum->top->h));
    char fsd[32] = "";

    (void)fsd_format_duration_ex (fsd,
                                  sizeof (fsd),
                                  fabs (now - sum->starttime),
                                  2);

    wattron (sum->win, A_DIM);
    mvwprintw (sum->win,
               info_dim.y_begin,
               info_dim.x_begin,
               "size: %d",
               sum->instance_size);
    if (sum->instance_level)
        mvwprintw (sum->win,
                   info_dim.y_begin,
                   info_dim.x_begin + 10,
                   "depth: %d",
                   sum->instance_level);
    mvwprintw (sum->win,
               info_dim.y_begin,
               info_dim.x_begin + 30,
               "uptime: %s",
               fsd);
    mvwprintw (sum->win,
               info_dim.y_begin,
               info_dim.x_begin +
                   info_dim.x_length - strlen (sum->instance_version),
               "%s",
               sum->instance_version);
    wattroff (sum->win, A_DIM);
}

/* Fetch expiration time (abs time relative to UNIX epoch) from resource.R.
 * If unavailable (e.g. we are a guest in the system instance), return 0.
 */
static double get_expiration (flux_t *h)
{
    flux_future_t *f;
    double val = 0;

    if (!(f = flux_kvs_lookup (h, NULL, 0, "resource.R"))
        || flux_kvs_lookup_get_unpack (f,
                                       "{s:{s:f}}",
                                       "execution",
                                       "expiration", &val) < 0) {
        if (errno == EPERM)
            goto done;
        fatal (errno, "error fetching or decoding resource.R");
    }
done:
    flux_future_destroy (f);
    return val;
}

static int get_instance_attr_int (flux_t *h, const char *attr)
{
    const char *s;
    unsigned long level;

    if (!(s = flux_attr_get (h, attr)))
        fatal (errno, "error fetching %s broker attribute", attr);
    errno = 0;
    level = strtoul (s, NULL, 10);
    if (errno != 0)
        fatal (errno, "error parsing %s", attr);
    return level;
}


static int resource_count (json_t *o,
                           const char *name,
                           int *nnodes,
                           int *ncores,
                           int *ngpus,
                           json_t *queue_constraint)
{
    json_t *R;
    struct rlist *rl_all = NULL;
    struct rlist *rl_constraint = NULL;
    struct rlist *rl;

    if (!(R = json_object_get (o, name)))
        return -1;
    if (json_is_null (R)) { // N.B. fluxion sets objects to json null if empty
        *nnodes = *ncores = *ngpus = 0;
        return 0;
    }
    if (!(rl_all = rlist_from_json (R, NULL)))
        return -1;
    if (queue_constraint) {
        flux_error_t error;
        rl_constraint = rlist_copy_constraint (rl_all,
                                               queue_constraint,
                                               &error);
        if (!rl_constraint)
            fatal (errno, "failed to create constrained rlist: %s", error.text);
        rl = rl_constraint;
    }
    else
        rl = rl_all;
    *nnodes = rlist_nnodes (rl);
    *ncores = rlist_count (rl, "core");
    *ngpus = rlist_count (rl, "gpu");
    rlist_destroy (rl_all);
    rlist_destroy (rl_constraint);
    return 0;
}

static void resource_continuation (flux_future_t *f, void *arg)
{
    struct summary_pane *sum = arg;
    json_t *o;

    if (flux_rpc_get_unpack (f, "o", &o) < 0) {
        if (errno != ENOSYS) /* Instance may not be up yet */
            fatal (errno, "resource.sched-status RPC failed");
    }
    else {
        json_t *queue_constraint;
        /* can return NULL constraint for "none" */
        queues_get_queue_constraint (sum->top->queues, &queue_constraint);
        if (resource_count (o,
                            "all",
                            &sum->node.total,
                            &sum->core.total,
                            &sum->gpu.total,
                            queue_constraint) < 0
            || resource_count (o,
                               "allocated",
                               &sum->node.used,
                               &sum->core.used,
                               &sum->gpu.used,
                               queue_constraint) < 0
            || resource_count (o,
                               "down",
                               &sum->node.down,
                               &sum->core.down,
                               &sum->gpu.down,
                               queue_constraint) < 0)
            fatal (0, "error decoding resource.sched-status RPC response");
    }
    flux_future_destroy (f);
    sum->f_resource = NULL;
    draw_resource (sum);
    if (sum->top->test_exit) {
        /* Ensure resources are refreshed before exiting */
        wnoutrefresh (sum->win);
        test_exit_check (sum->top);
    }
}

static int get_queue_stats (json_t *o, const char *queue_name, json_t **qstats)
{
    json_t *queues = NULL;
    json_t *q = NULL;
    size_t index;
    json_t *value;
    if (json_unpack (o, "{s:o}", "queues", &queues) < 0)
        return -1;
    if (json_is_array (queues) == 0)
        return -1;
    json_array_foreach (queues, index, value) {
        const char *name;
        if (json_unpack (value, "{s:s}", "name", &name) < 0)
            return -1;
        if (streq (queue_name, name)) {
            q = value;
            break;
        }
    }
    (*qstats) = q;
    return 0;
}

void summary_pane_jobstats (struct summary_pane *sum, flux_future_t *f)
{
    json_t *o = NULL;
    const char *filter_queue;

    if (flux_rpc_get_unpack (f, "o", &o) < 0) {
        if (errno != ENOSYS)
            fatal (errno, "error getting job-list.job-stats RPC response");
    }

    /* can return NULL filter_queue for "all" queues */
    queues_get_queue_name (sum->top->queues, &filter_queue);
    if (filter_queue) {
        json_t *qstats = NULL;
        if (get_queue_stats (o, filter_queue, &qstats) < 0)
            fatal (EPROTO, "error parsing queue stats");
        /* stats may not yet exist if no jobs submitted to the queue */
        if (!qstats)
            goto out;
        o = qstats;
    }

    if (json_unpack (o,
                     "{s:i s:i s:i s:i s:{s:i s:i s:i s:i s:i s:i s:i}}",
                     "successful", &sum->stats.successful,
                     "failed", &sum->stats.failed,
                     "canceled", &sum->stats.canceled,
                     "timeout", &sum->stats.timeout,
                     "job_states",
                       "depend", &sum->stats.depend,
                       "priority", &sum->stats.priority,
                       "sched", &sum->stats.sched,
                       "run", &sum->stats.run,
                       "cleanup", &sum->stats.cleanup,
                       "inactive", &sum->stats.inactive,
                       "total", &sum->stats.total) < 0)
        fatal (0, "error decoding job-list.job-stats object");

out:
    draw_stats (sum);
    if (sum->top->test_exit) {
        /* Ensure stats is refreshed before exiting */
        wnoutrefresh (sum->win);
        test_exit_check (sum->top);
    }
}

static void heartblink_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct summary_pane *sum = arg;

    sum->heart_visible = false;
    draw_heartbeat (sum);
}

void summary_pane_heartbeat (struct summary_pane *sum)
{
    sum->heart_visible = true;
    flux_timer_watcher_reset (sum->heartblink, heartblink_duration, 0.);
    flux_watcher_start (sum->heartblink);
}

static void resource_retry_cb (flux_future_t *f, void *arg)
{
    flux_future_t *result = arg;
    flux_future_fulfill_with (result, f);
    flux_future_destroy (f);
}

static void resource_enosys_check_cb (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    flux_future_t *fretry = NULL;
    flux_future_t *result = arg;

    if (flux_future_get (f, NULL) == 0 || errno != ENOSYS) {
        flux_future_fulfill_with (result, f);
        return;
    }
    /*  The RPC failed with ENOSYS. Retry with sched.resource-status:
     */
    if (!(fretry = flux_rpc (h, "sched.resource-status", NULL, 0, 0))
        || flux_future_then (fretry, -1., resource_retry_cb, result) < 0) {
        flux_future_fulfill_error (result, errno, NULL);
        flux_future_destroy (fretry);
    }
}

flux_future_t *resource_sched_status (struct summary_pane *sum)
{
    flux_future_t *result = NULL;
    flux_future_t *f = NULL;

    /* Create empty future to contain result from either resource.sched-status
     * or sched.resource-status RPC:
     */
    if (!(result = flux_future_create (NULL, NULL))
        || !(f = flux_rpc (sum->top->h, "resource.sched-status", NULL, 0, 0))
        || flux_future_then (f, -1., resource_enosys_check_cb, result) < 0)
        goto error;

    flux_future_set_flux (result, sum->top->h);
    return result;
error:
    flux_future_destroy (result);
    flux_future_destroy (f);
    return NULL;
}

/* Send a query.
 * If there's already one pending, do nothing.
 */
void summary_pane_query (struct summary_pane *sum)
{
    if (!sum->f_resource) {
        if (!(sum->f_resource = resource_sched_status (sum))
            || flux_future_then (sum->f_resource,
                                 -1,
                                 resource_continuation,
                                 sum) < 0) {
            flux_future_destroy (sum->f_resource);
            sum->f_resource = NULL;
        }
    }
}

void summary_pane_toggle_details (struct summary_pane *sum)
{
    sum->show_details = !sum->show_details;
    summary_pane_draw (sum);
}

void summary_pane_draw (struct summary_pane *sum)
{
    werase (sum->win);
    draw_f (sum);
    draw_title (sum);
    draw_timeleft (sum);
    draw_resource (sum);
    draw_stats (sum);
    draw_info (sum);
    draw_heartbeat (sum);
}

void summary_pane_refresh (struct summary_pane *sum)
{
    wnoutrefresh (sum->win);
}

struct summary_pane *summary_pane_create (struct top *top)
{
    struct summary_pane *sum;
    flux_reactor_t *r = flux_get_reactor (top->h);

    if (!(sum = calloc (1, sizeof (*sum))))
        fatal (errno, "error creating context for summary pane");
    if (!(sum->heartblink = flux_timer_watcher_create (r,
                                                       heartblink_duration,
                                                       0.,
                                                       heartblink_cb,
                                                       sum)))
        fatal (errno, "error creating timer for heartbeat blink");
    if (!(sum->win = newwin (win_dim.y_length,
                             win_dim.x_length,
                             win_dim.y_begin,
                             win_dim.x_begin)))
        fatal (0, "error creating curses window for summary pane");
    sum->top = top;

    sum->expiration = get_expiration (top->h);
    sum->instance_level = get_instance_attr_int (top->h, "instance-level");
    sum->instance_size = get_instance_attr_int (top->h, "size");
    sum->instance_version = flux_attr_get (top->h, "version");
    if (flux_get_instance_starttime (top->h, &sum->starttime) < 0)
        sum->starttime = flux_reactor_now (flux_get_reactor (top->h));

    sum->owner = get_instance_attr_int (top->h, "security.owner");
    if (sum->owner == getuid ())
        sum->show_details = true;

    summary_pane_query (sum);
    summary_pane_draw (sum);
    summary_pane_refresh (sum);
    return sum;
}

void summary_pane_destroy (struct summary_pane *sum)
{
    if (sum) {
        int saved_errno = errno;
        flux_future_destroy (sum->f_resource);
        flux_watcher_destroy (sum->heartblink);
        delwin (sum->win);
        free (sum);
        errno = saved_errno;
    }
}

// vi:ts=4 sw=4 expandtab
