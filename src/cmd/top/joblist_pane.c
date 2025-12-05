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
#include <math.h>
#include <jansson.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"

#include "top.h"

static const struct dimension win_dim = { 0, 6, 80, 60 };

struct joblist_pane {
    struct top *top;
    int jobid_width;
    WINDOW *win;
    json_t *jobs_all;
    json_t *jobs_query;
    json_t *jobs;
    struct ucache *ucache;

    bool show_queue;

    /*  Currently selected jobid. Ironically FLUX_JOBID_ANY means
     *   no current selection.
     */
    flux_jobid_t current;
};

static int lookup_jobid_index (json_t *jobs, flux_jobid_t id)
{
    int result = -1;
    size_t index;
    json_t *job;

    if (jobs) {
        json_array_foreach (jobs, index, job) {
            flux_jobid_t jobid;

            if (json_unpack (job, "{s:I}", "id", &jobid) < 0)
                continue;
            if (jobid == id)
                return (int) index;
        }
    }
    return result;
}

static json_t *get_current_job (struct joblist_pane *joblist)
{
    int index = lookup_jobid_index (joblist->jobs,
                                    joblist->current);
    return json_array_get (joblist->jobs, index);
}


void joblist_pane_draw (struct joblist_pane *joblist)
{
    double now = flux_reactor_now (flux_get_reactor (joblist->top->h));
    size_t index;
    json_t *job;
    int queue_width = joblist->show_queue ? 8 : 0;
    int name_width;
    int job_output_count = 0;

    werase (joblist->win);
    wattron (joblist->win, A_REVERSE);

    name_width = getmaxx (joblist->win)
                 - (12 + 8 + queue_width + 2 + 6 + 6 + 7 + 6);
    if (joblist->show_queue)
        mvwprintw (joblist->win,
                   0,
                   0,
                   "%*s %8s %8s %2s %6s %6s %7s %-*s",
                   joblist->jobid_width,
                   "JOBID", "QUEUE", "USER", "ST",
                   "NTASKS", "NNODES", "RUNTIME",
                   name_width, "NAME");
    else
        mvwprintw (joblist->win,
                   0,
                   0,
                   "%*s %8s %2s %6s %6s %7s %-*s",
                   joblist->jobid_width,
                   "JOBID","USER", "ST", "NTASKS", "NNODES", "RUNTIME",
                   name_width, "NAME");

    wattroff (joblist->win, A_REVERSE);
    if (joblist->jobs == NULL)
        return;

    if (json_array_size (joblist->jobs) == 0
        && queues_configured (joblist->top->queues)) {
        const char *filter_queue = NULL;
        /* can return NULL filter_queue for "all" queues */
        queues_get_queue_name (joblist->top->queues, &filter_queue);
        if (filter_queue)
            mvwprintw (joblist->win,
                       5,
                       25,
                       "No jobs to display in queue %s",
                       filter_queue);
        return;
    }

    json_array_foreach (joblist->jobs, index, job) {
        char *uri = NULL;
        const char *idstr;
        char run[16] = "";
        flux_jobid_t id;
        int userid;
        const char *username;
        const char *name;
        const char *queue = "";
        int state;
        int ntasks;
        int nnodes;
        double t_run;

        /* A running job is in the RUN or CLEANUP state.  Under racy circumstances
         * a job could end up in the CLEANUP state without having entered the
         * RUN state and some fields below would not be available.  If we fail
         * to unpack this data, simply continue onto the next job.
         */

        if (json_unpack (job,
                         "{s:I s:i s:i s:s s?s s:i s:i s:f s?{s?{s?s}}}",
                         "id", &id,
                         "userid", &userid,
                         "state", &state,
                         "name", &name,
                         "queue", &queue,
                         "nnodes", &nnodes,
                         "ntasks", &ntasks,
                         "t_run", &t_run,
                         "annotations",
                           "user",
                             "uri", &uri) < 0)
            continue;

        idstr = idf58 (id);
        (void)fsd_format_duration_ex (run, sizeof (run), fabs (now - t_run), 2);
        if (!(username = ucache_lookup (joblist->ucache, userid)))
            fatal (errno, "error looking up userid %d in ucache", (int)userid);

        /*  Highlight current selection in blue, o/w color jobs that
         *   are Flux instances blue (and bold for non-color terminals)
         */
        if (id == joblist->current)
            wattron (joblist->win, A_REVERSE);
        if (uri != NULL)
            wattron (joblist->win, COLOR_PAIR(TOP_COLOR_BLUE) | A_BOLD);
        if (joblist->show_queue) {
            mvwprintw (joblist->win,
                       1 + job_output_count,
                       0,
                        "%13.13s %8.8s %8.8s %2.2s %6d %6d %7.7s %-*.*s",
                       idstr,
                       queue,
                       username,
                       flux_job_statetostr (state, "S"),
                       ntasks,
                       nnodes,
                       run,
                       name_width,
                       name_width,
                       name);
            if (joblist->top->testf)
                fprintf (joblist->top->testf,
                         "%s %s %s %s %d %d %s %s\n",
                         idstr,
                         queue,
                         username,
                         flux_job_statetostr (state, "S"),
                         ntasks,
                         nnodes,
                         run,
                         name);
        }
        else {
            mvwprintw (joblist->win,
                       1 + job_output_count,
                       0,
                        "%13.13s %8.8s %2.2s %6d %6d %7.7s %-*.*s",
                       idstr,
                       username,
                       flux_job_statetostr (state, "S"),
                       ntasks,
                       nnodes,
                       run,
                       name_width,
                       name_width,
                       name);
            if (joblist->top->testf)
                fprintf (joblist->top->testf,
                         "%s %s %s %d %d %s %s\n",
                         idstr,
                         username,
                         flux_job_statetostr (state, "S"),
                         ntasks,
                         nnodes,
                         run,
                         name);
        }
        job_output_count++;
        wattroff (joblist->win, A_REVERSE);
        wattroff (joblist->win, COLOR_PAIR(TOP_COLOR_BLUE) | A_BOLD);
    }
}

void joblist_filter_jobs (struct joblist_pane *joblist)
{
    json_decref (joblist->jobs);

    if (queues_configured (joblist->top->queues)) {
        const char *filter_queue;
        /* can return NULL filter_queue for "all" queues */
        queues_get_queue_name (joblist->top->queues, &filter_queue);
        if (filter_queue) {
            json_t *a;
            size_t index;
            json_t *job;
            if (!(a = json_array ()))
                fatal (ENOMEM, "error creating joblist array");
            json_array_foreach (joblist->jobs_all, index, job) {
                const char *queue;
                /* skip jobs that don't have queue configured.  Potentially
                 * rare case in racy scenarios.
                 */
                if (json_unpack (job, "{s:s}", "queue", &queue) < 0)
                    continue;
                if (!streq (filter_queue, queue))
                    continue;
                if (json_array_append (a, job) < 0)
                    fatal (ENOMEM, "error appending job to joblist");
            }
            joblist->jobs = a;
            return;
        }
    }
    joblist->jobs = json_incref (joblist->jobs_all);
}

static void joblist_query_finish (struct joblist_pane *joblist)
{
    json_decref (joblist->jobs_all);
    joblist->jobs_all = joblist->jobs_query;
    joblist->jobs_query = NULL;
    joblist_filter_jobs (joblist);
    joblist_pane_draw (joblist);
    if (joblist->top->test_exit) {
        /* Ensure joblist window is refreshed before exiting */
        wrefresh (joblist->win);
        test_exit_check (joblist->top);
    }
}

static void joblist_continuation (flux_future_t *f, void *arg)
{
    struct joblist_pane *joblist = arg;
    json_t *jobs;
    size_t index;
    json_t *value;
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
        if (errno == ENODATA) {
            joblist_query_finish (joblist);
            flux_future_destroy (f);
            return;
        }
        if (errno != ENOSYS)
            fatal (errno, "error decoding job-list.list RPC response");
        flux_future_destroy (f);
        return;
    }
    if (!joblist->jobs_query) {
        if (!(joblist->jobs_query = json_array ()))
            fatal (ENOMEM, "error allocating jobs query array");
    }
    json_array_foreach (jobs, index, value) {
        if (json_array_append (joblist->jobs_query, value) < 0)
            fatal (ENOMEM, "error appending to jobs query array");
    }
    flux_future_reset (f);
}


/* Attempt to create a popup box over the joblist pane to
 *  display one or more errors. The box will stay open until
 *  the user presses a key.
 */
static void error_popup (struct joblist_pane *joblist,
                         const char *msg)
{
    WINDOW *popup = newwin (6, 78, 15, 2);
    WINDOW *errors = NULL;
    if (!popup)
        goto out;
    box (popup, 0, 0);
    touchwin (popup);
    overwrite (popup, joblist->win);

    if (!(errors = derwin (popup, 3, 75, 2, 2)))
        goto out;

    mvwprintw (errors, 0, 0, "%s", msg);

    /*  Refresh windows
     */
    wrefresh (popup);
    wrefresh (errors);

    /*  Display error for up to 4s. Any key exits prematurely */
    halfdelay (40);
    getch ();

    /* Leave halfdelay mode */
    nocbreak ();
    cbreak ();

out:
    if (popup)
        delwin (popup);
    if (errors)
        delwin (errors);
}

void joblist_pane_enter (struct joblist_pane *joblist)
{
    struct top *top;
    flux_jobid_t id;
    char *uri = NULL;
    char title [1024];
    flux_error_t error;

    json_t *job = get_current_job (joblist);
    if (!job)
        return;
    if (json_unpack (job,
                     "{s:I s:{s:{s:s}}}",
                     "id", &id,
                     "annotations",
                       "user",
                         "uri", &uri) < 0)
        return;
    if (uri == NULL)
        return;
    if (snprintf (title,
                  sizeof(title),
                  "%s/%s",
                  joblist->top->title,
                  idf58 (id)) > sizeof (title))
        fatal (errno, "failed to build job title for job");

    /*  Lazily attempt to run top on jobid, but for now simply return to the
     *   original top window on failure.
     */
    if ((top = top_create (uri, title, NULL, &error)))
        top_run (top, 0);
    else
        error_popup (joblist, error.text);
    top_destroy (top);
    return;
}

void joblist_pane_query (struct joblist_pane *joblist)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (joblist->top->h,
                             "job-list.list",
                             0,
                             FLUX_RPC_STREAMING,
                             "{s:i s:{s:[i]} s:[s,s,s,s,s,s,s,s]}",
                             "max_entries", win_dim.y_length - 1,
                             "constraint",
                             "states", FLUX_JOB_STATE_RUNNING,
                             "attrs",
                               "annotations",
                               "userid",
                               "state",
                               "name",
                               "queue",
                               "nnodes",
                               "ntasks",
                               "t_run"))
        || flux_future_then (f, -1, joblist_continuation, joblist) < 0)
        fatal (errno, "error sending job-list.list RPC request");
}

void joblist_pane_refresh (struct joblist_pane *joblist)
{
    wnoutrefresh (joblist->win);
}

void joblist_pane_set_current (struct joblist_pane *joblist, bool next)
{
    int index = -1;
    json_t *job;
    flux_jobid_t id = FLUX_JOBID_ANY;
    int njobs;
    int next_index;

    if (joblist->jobs == NULL)
        return;

    if (joblist->current != FLUX_JOBID_ANY)
        index = lookup_jobid_index (joblist->jobs, joblist->current);

    /* find next valid index */
    njobs = json_array_size (joblist->jobs);
    next_index = next ? index + 1 : index - 1;

    /* wrap around to top/bottom if index out of range */
    if (next_index == njobs)
        next_index = 0;
    else if (next_index < 0)
        next_index = njobs - 1;

    if (!(job = json_array_get (joblist->jobs, next_index)))
        return;

    if (job && json_unpack (job, "{s:I}", "id", &id) < 0)
        return;

    if (id != joblist->current) {
        joblist->current = id;
        joblist_pane_draw (joblist);
    }
}

/*
 *  Workaround for mvwprintw(3) issues with multibyte jobid 'ƒ' character.
 *
 *  Empirically, the JOBID column must be formatted as %12s when the 'ƒ'
 *  character appears in f58 encoded jobids, but %13s when ascii 'f' is used.
 *  Guess at the current jobid encoding by determining if the f58 encoding
 *  of jobid 0 has a length of 2 (ascii) or 3 (utf-8).
 *
 */
static int estimate_jobid_width (void)
{
    const char *id = idf58 (0);
    if (strlen (id) == 2)
        return 13;
    else
        return 12;
}

struct joblist_pane *joblist_pane_create (struct top *top)
{
    struct joblist_pane *joblist;

    if (!(joblist = calloc (1, sizeof (*joblist))))
        fatal (errno, "could not allocate joblist context");
    if (!(joblist->ucache = ucache_create ()))
        fatal (errno, "could not create ucache");
    joblist->top = top;
    joblist->jobid_width = estimate_jobid_width ();
    joblist->current = FLUX_JOBID_ANY;
    joblist->show_queue = queues_configured (top->queues);
    if (!(joblist->win = newwin (win_dim.y_length,
                                 win_dim.x_length,
                                 win_dim.y_begin,
                                 win_dim.x_begin)))
        fatal (0, "error creating joblist curses window");
    joblist_pane_query (joblist);
    joblist_pane_draw (joblist);
    joblist_pane_refresh (joblist);
    return joblist;
}

void joblist_pane_destroy (struct joblist_pane *joblist)
{
    if (joblist) {
        int saved_errno = errno;
        delwin (joblist->win);
        ucache_destroy (joblist->ucache);
        json_decref (joblist->jobs_all);
        json_decref (joblist->jobs);
        free (joblist);
        errno = saved_errno;
    }
}

// vi:ts=4 sw=4 expandtab
