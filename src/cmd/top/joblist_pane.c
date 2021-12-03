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

#include "src/common/libutil/fsd.h"

#include "top.h"

static const struct dimension win_dim = { 0, 5, 80, 60 };

struct joblist_pane {
    struct top *top;
    WINDOW *win;
    json_t *jobs;
    struct ucache *ucache;
};

void joblist_pane_draw (struct joblist_pane *joblist)
{
    double now = flux_reactor_now (flux_get_reactor (joblist->top->h));
    size_t index;
    json_t *job;
    int name_width = getmaxx (joblist->win) - (12 + 8 + 2 + 6 + 6 + 7 + 6);

    werase (joblist->win);
    wattron (joblist->win, A_REVERSE);
    mvwprintw (joblist->win,
               0,
               0,
               "%12s %8s %2s %6s %6s %7s %-*s",
               "JOBID", "USER", "ST", "NTASKS", "NNODES", "RUNTIME",
               name_width, "NAME");
    wattroff (joblist->win, A_REVERSE);
    if (joblist->jobs == NULL)
        return;
    json_array_foreach (joblist->jobs, index, job) {
        char idstr[16];
        char run[16];
        flux_jobid_t id;
        int userid;
        const char *username;
        const char *name;
        int state;
        int ntasks;
        int nnodes;
        double t_run;

        if (json_unpack (job,
                         "{s:I s:i s:i s:s s:i s:i s:f}",
                         "id", &id,
                         "userid", &userid,
                         "state", &state,
                         "name", &name,
                         "nnodes", &nnodes,
                         "ntasks", &ntasks,
                         "t_run", &t_run) < 0)
            fatal (errno, "error decoding a job record from job-list RPC");
        if (flux_job_id_encode (id, "f58", idstr, sizeof (idstr)) < 0)
            fatal (errno, "error encoding jobid as F58");
        if (fsd_format_duration_ex (run, sizeof (run), now - t_run, 2) < 0)
            fatal (errno, "error formating expiration time as FSD");
        if (!(username = ucache_lookup (joblist->ucache, userid)))
            fatal (errno, "error looking up userid %d in ucache", (int)userid);
        mvwprintw (joblist->win,
                   1 + index,
                   0,
                   "%13.13s %8.8s %2.2s %6d %6d %7.7s %-*.*s",
                   idstr,
                   username,
                   flux_job_statetostr (state, true),
                   ntasks,
                   nnodes,
                   run,
                   name_width,
                   name_width,
                   name);
    }
}

static void joblist_continuation (flux_future_t *f, void *arg)
{
    struct joblist_pane *joblist = arg;
    json_t *jobs;

    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        fatal (errno, "error decoding job-list.list RPC response");
    json_decref (joblist->jobs);
    joblist->jobs = json_incref (jobs);
    joblist_pane_draw (joblist);
    flux_future_destroy (f);
}

void joblist_pane_query (struct joblist_pane *joblist)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (joblist->top->h,
                             "job-list.list",
                             0,
                             0,
                             "{s:i s:i s:i s:i s:[s,s,s,s,s,s]}",
                             "max_entries", win_dim.y_length - 1,
                             "userid", FLUX_USERID_UNKNOWN,
                             "states", FLUX_JOB_STATE_RUNNING,
                             "results", 0,
                             "attrs",
                               "userid",
                               "state",
                               "name",
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

struct joblist_pane *joblist_pane_create (struct top *top)
{
    struct joblist_pane *joblist;

    if (!(joblist = calloc (1, sizeof (*joblist))))
        fatal (errno, "could not allocate joblist context");
    if (!(joblist->ucache = ucache_create ()))
        fatal (errno, "could not create ucache");
    joblist->top = top;
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
