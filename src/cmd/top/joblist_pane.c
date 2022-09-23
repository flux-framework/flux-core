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

#include "top.h"

static const struct dimension win_dim = { 0, 6, 80, 60 };

struct joblist_pane {
    struct top *top;
    WINDOW *win;
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


/*  Return true if any job in joblist->jobs has a queue defined. If
 *  joblist->jobs is NULL or empty, then return the current show_queue
 *  value.
 */
static bool show_queue (struct joblist_pane *joblist)
{
    size_t index;
    json_t *job;

    if (!joblist->jobs || json_array_size (joblist->jobs) == 0)
        return joblist->show_queue;

    json_array_foreach (joblist->jobs, index, job) {
        if (json_object_get (job, "queue"))
            return true;
    }
    return false;
}

void joblist_pane_draw (struct joblist_pane *joblist)
{
    double now = flux_reactor_now (flux_get_reactor (joblist->top->h));
    size_t index;
    json_t *job;
    int queue_width = 0;
    int name_width;

    werase (joblist->win);
    wattron (joblist->win, A_REVERSE);

    if ((joblist->show_queue = show_queue (joblist)))
        queue_width = 8;

    name_width = getmaxx (joblist->win)
                 - (12 + 8 + queue_width + 2 + 6 + 6 + 7 + 6);
    if (joblist->show_queue)
        mvwprintw (joblist->win,
                   0,
                   0,
                   "%12s %8s %8s %2s %6s %6s %7s %-*s",
                   "JOBID", "QUEUE", "USER", "ST",
                   "NTASKS", "NNODES", "RUNTIME",
                   name_width, "NAME");
    else
        mvwprintw (joblist->win,
                   0,
                   0,
                   "%12s %8s %2s %6s %6s %7s %-*s",
                   "JOBID","USER", "ST", "NTASKS", "NNODES", "RUNTIME",
                   name_width, "NAME");

    wattroff (joblist->win, A_REVERSE);
    if (joblist->jobs == NULL)
        return;
    json_array_foreach (joblist->jobs, index, job) {
        char *uri = NULL;
        char idstr[16];
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
            fatal (0, "error decoding a job record from job-list RPC");
        if (flux_job_id_encode (id, "f58", idstr, sizeof (idstr)) < 0)
            fatal (errno, "error encoding jobid as F58");
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
        if (joblist->show_queue)
            mvwprintw (joblist->win,
                       1 + index,
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
        else
            mvwprintw (joblist->win,
                       1 + index,
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
        wattroff (joblist->win, A_REVERSE);
        wattroff (joblist->win, COLOR_PAIR(TOP_COLOR_BLUE) | A_BOLD);
    }
}

static void joblist_continuation (flux_future_t *f, void *arg)
{
    struct joblist_pane *joblist = arg;
    json_t *jobs;

    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
        if (errno != ENOSYS)
            fatal (errno, "error decoding job-list.list RPC response");
        flux_future_destroy (f);
        return;
    }
    json_decref (joblist->jobs);
    joblist->jobs = json_incref (jobs);
    joblist_pane_draw (joblist);
    if (joblist->top->test_exit) {
        /* Ensure joblist window is refreshed before exiting */
        wrefresh (joblist->win);
        flux_reactor_stop (flux_future_get_reactor (f));
    }
    flux_future_destroy (f);
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
    char jobid [24];
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
    if (flux_job_id_encode (id, "f58", jobid, sizeof (jobid)) < 0
        || snprintf (title,
                     sizeof(title),
                     "%s/%s",
                     joblist->top->title,
                     jobid) > sizeof (title))
        fatal (errno, "failed to build job title for job");

    /*  Lazily attempt to run top on jobid, but for now simply return to the
     *   original top window on failure.
     */
    if ((top = top_create (uri, title, &error)))
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
                             0,
                             "{s:i s:i s:i s:i s:[s,s,s,s,s,s,s,s]}",
                             "max_entries", win_dim.y_length - 1,
                             "userid", FLUX_USERID_UNKNOWN,
                             "states", FLUX_JOB_STATE_RUNNING,
                             "results", 0,
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
    int current = -1;;
    json_t *job;
    flux_jobid_t id = FLUX_JOBID_ANY;
    int njobs;

    if (joblist->jobs == NULL)
        return;

    if (joblist->current != FLUX_JOBID_ANY)
        current = lookup_jobid_index (joblist->jobs, joblist->current);

    njobs = json_array_size (joblist->jobs);
    if (next && current == njobs -1)
        current = -1;
    else if (!next && current <= 0)
        current = njobs;

    if (!(job = json_array_get (joblist->jobs,
                                next ? current + 1 : current - 1)))
        return;
    if (job && json_unpack (job, "{s:I}", "id", &id) < 0)
        return;

    if (id != joblist->current) {
        joblist->current = id;
        joblist_pane_draw (joblist);
    }
}

struct joblist_pane *joblist_pane_create (struct top *top)
{
    struct joblist_pane *joblist;

    if (!(joblist = calloc (1, sizeof (*joblist))))
        fatal (errno, "could not allocate joblist context");
    if (!(joblist->ucache = ucache_create ()))
        fatal (errno, "could not create ucache");
    joblist->top = top;
    joblist->current = FLUX_JOBID_ANY;
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
        json_decref (joblist->jobs);
        free (joblist);
        errno = saved_errno;
    }
}

// vi:ts=4 sw=4 expandtab
