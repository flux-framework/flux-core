/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <czmq.h>
#include <flux/core.h>
#include <flux/idset.h>
#include <flux/schedutil.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libjob/job.h"
#include "libjj.h"
#include "rlist.h"

struct jobreq {
    void *handle;
    const flux_msg_t *msg;
    uint32_t uid;
    int priority;
    double t_submit;
    flux_jobid_t id;
    struct jj_counts jj;
    int errnum;
};

struct simple_sched {
    flux_t *h;
    flux_future_t *acquire_f; /* resource.acquire future */

    char *mode;             /* allocation mode */
    bool single;
    bool sched_pus;         /* schedule PUs as cores */
    struct rlist *rlist;    /* list of resources */
    zlistx_t *queue;        /* job queue */
    schedutil_t *util_ctx;

    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
};

static void jobreq_destroy (struct jobreq *job)
{
    if (job) {
        flux_msg_decref (job->msg);
        ERRNO_SAFE_WRAP (free, job);
    }
}

static void jobreq_destructor (void **x)
{
    jobreq_destroy (*x);
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Taken from modules/job-manager/job.c */
static int jobreq_cmp (const void *x, const void *y)
{
    const struct jobreq *j1 = x;
    const struct jobreq *j2 = y;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->t_submit, j2->t_submit);
    return rc;
}

static struct jobreq *
jobreq_find (struct simple_sched *ss, flux_jobid_t id)
{
    struct jobreq *job;
    job = zlistx_first (ss->queue);
    while (job) {
        if (job->id == id)
            return job;
        job = zlistx_next (ss->queue);
    }
    return NULL;
}

static struct jobreq *
jobreq_create (const flux_msg_t *msg, const char *jobspec)
{
    struct jobreq *job = calloc (1, sizeof (*job));

    if (job == NULL)
        return NULL;
    if (schedutil_alloc_request_decode (msg,
                                        &job->id,
                                        &job->priority,
                                        &job->uid,
                                        &job->t_submit) < 0)
        goto err;
    job->msg = flux_msg_incref (msg);
    if (libjj_get_counts (jobspec, &job->jj) < 0)
        job->errnum = errno;
    return job;
err:
    jobreq_destroy (job);
    return NULL;
}

static void simple_sched_destroy (flux_t *h, struct simple_sched *ss)
{
    struct jobreq *job = zlistx_first (ss->queue);
    while (job) {
        flux_respond_error (h, job->msg, ENOSYS, "simple sched exiting");
        job = zlistx_next (ss->queue);
    }
    flux_future_destroy (ss->acquire_f);
    zlistx_destroy (&ss->queue);
    flux_watcher_destroy (ss->prep);
    flux_watcher_destroy (ss->check);
    flux_watcher_destroy (ss->idle);
    schedutil_destroy (ss->util_ctx);
    rlist_destroy (ss->rlist);
    free (ss->mode);
    free (ss);
}

static struct simple_sched * simple_sched_create (void)
{
    struct simple_sched *ss = calloc (1, sizeof (*ss));
    if (ss == NULL)
        return NULL;

    /* Single alloc request mode is default */
    ss->single = true;
    return ss;
}

static int R_add_expiration (json_t *R, double now, double timelimit)
{
    int rc = -1;
    json_t *exec = NULL;
    json_t *o = NULL;
    double starttime = now;
    double expiration = now + timelimit;

    if (!(exec = json_object_get (R, "execution"))
        || (!(o = json_real (starttime)))
        || json_object_set_new (exec, "starttime", o) < 0) {
        json_decref (o);
        goto out;
    }
    if (!(o = json_real (expiration))
        || json_object_set_new (exec, "expiration", o) < 0) {
        json_decref (o);
        goto out;
    }
    rc = 0;
out:
    return rc;
}

static char *Rstring_create (struct rlist *l, double now, double timelimit)
{
    char *s = NULL;
    json_t *R = rlist_to_R (l);
    if (R) {
        if (timelimit > 0. &&  R_add_expiration (R, now, timelimit) < 0)
            goto out;
        s = json_dumps (R, JSON_COMPACT);
out:
        json_decref (R);
    }
    return (s);
}

static int try_alloc (flux_t *h, struct simple_sched *ss)
{
    int rc = -1;
    char *s = NULL;
    struct rlist *alloc = NULL;
    struct jj_counts *jj = NULL;
    char *R = NULL;
    struct jobreq *job = zlistx_first (ss->queue);
    double now = flux_reactor_now (flux_get_reactor (h));

    if (!job)
        return -1;
    jj = &job->jj;
    alloc = rlist_alloc (ss->rlist, ss->mode,
                         jj->nnodes, jj->nslots, jj->slot_size);
    if (!alloc || !(R = Rstring_create (alloc, now, jj->duration))) {
        const char *note = "unable to allocate provided jobspec";
        if (alloc != NULL) {
            /*  unlikely: allocation succeeded but Rstring_create failed */
            note = "internal scheduler error generating R";
            flux_log (ss->h, LOG_ERR, "%s", note);
            if (rlist_free (ss->rlist, alloc) < 0)
                flux_log_error (h, "try_alloc: rlist_free");
            rlist_destroy (alloc);
            alloc = NULL;
        } else if (errno == ENOSPC)
            return rc;
        else if (errno == EOVERFLOW)
            note = "unsatisfiable request";
        if (schedutil_alloc_respond_deny (ss->util_ctx,
                                          job->msg,
                                          note) < 0)
            flux_log_error (h, "schedutil_alloc_respond_deny");
        goto out;
    }
    s = rlist_dumps (alloc);

    if (schedutil_alloc_respond_success_pack (ss->util_ctx,
                                              job->msg,
                                              R,
                                              "{ s:{s:s} }",
                                              "sched", "resource_summary", s) < 0)
        flux_log_error (h, "schedutil_alloc_respond_success_pack");

    flux_log (h, LOG_DEBUG, "alloc: %ju: %s", (uintmax_t) job->id, s);
    rc = 0;

out:
    zlistx_delete (ss->queue, job->handle);
    rlist_destroy (alloc);
    free (R);
    free (s);
    return rc;
}

static void prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct simple_sched *ss = arg;
    /* if there is at least one job to schedule, start check and idle */
    if (zlistx_size (ss->queue) > 0) {
        /* If there's a new job to process, start idle watcher */
        flux_watcher_start (ss->check);
        flux_watcher_start (ss->idle);
    }
}

static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct simple_sched *ss = arg;
    flux_watcher_stop (ss->idle);

    /* See if we can fulfill alloc for a pending job
     * If current head of queue can't be allocated, stop the prep
     *  watcher, i.e. block. O/w, retry on next loop.
     */
    if (try_alloc (ss->h, ss) < 0 && errno == ENOSPC) {
        flux_watcher_stop (ss->prep);
        flux_watcher_stop (ss->check);
    }
}

static int try_free (flux_t *h, struct simple_sched *ss, const char *R)
{
    int rc = -1;
    char *r = NULL;
    struct rlist *alloc = rlist_from_R (R);
    if (!alloc) {
        flux_log_error (h, "hello: unable to parse R=%s", R);
        return -1;
    }
    r = rlist_dumps (alloc);
    if ((rc = rlist_free (ss->rlist, alloc)) < 0)
        flux_log_error (h, "free: %s", r);
    else
        flux_log (h, LOG_DEBUG, "free: %s", r);
    free (r);
    rlist_destroy (alloc);
    return rc;
}

void free_cb (flux_t *h, const flux_msg_t *msg, const char *R, void *arg)
{
    struct simple_sched *ss = arg;

    if (try_free (h, ss, R) < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "free_cb: flux_respond_error");
        return;
    }
    if (schedutil_free_respond (ss->util_ctx, msg) < 0)
        flux_log_error (h, "free_cb: schedutil_free_respond");

    /* See if we can fulfill alloc for a pending job */
    flux_watcher_start (ss->prep);
}

static void alloc_cb (flux_t *h, const flux_msg_t *msg,
                      const char *jobspec, void *arg)
{
    struct simple_sched *ss = arg;
    struct jobreq *job;

    if (ss->single && zlistx_size (ss->queue) > 0) {
        flux_log (h, LOG_ERR, "alloc received before previous one handled");
        errno = EINVAL;
        goto err;
    }
    if (!(job = jobreq_create (msg, jobspec))) {
        flux_log_error (h, "alloc: jobreq_create");
        goto err;
    }
    if (job->errnum != 0) {
        if (schedutil_alloc_respond_deny (ss->util_ctx,
                                          msg,
                                          job->jj.error) < 0)
            flux_log_error (h, "alloc_respond_deny");
        jobreq_destroy (job);
        return;
    }
    flux_log (h, LOG_DEBUG, "req: %ju: spec={%d,%d,%d} duration=%.1f",
                            (uintmax_t) job->id, job->jj.nnodes,
                            job->jj.nslots, job->jj.slot_size,
                            job->jj.duration);
    job->handle = zlistx_insert (ss->queue,
                                 job,
                                 job->priority > FLUX_JOB_PRIORITY_DEFAULT);
    flux_watcher_start (ss->prep);
    return;
err:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "alloc: flux_respond_error");
}

/* Job manager wants to cancel a pending allocation request.
 * If a matching job found in queue, respond to the alloc request
 * and "dequeue" it.
 */
static void cancel_cb (flux_t *h,
                       flux_jobid_t id,
                       void *arg)
{
    struct simple_sched *ss = arg;
    struct jobreq *job = jobreq_find (ss, id);

    if (job) {
        if (schedutil_alloc_respond_cancel (ss->util_ctx, job->msg) < 0) {
            flux_log_error (h, "alloc_respond_cancel");
            return;
        }
        zlistx_delete (ss->queue, job->handle);
    }
}

static int hello_cb (flux_t *h,
                     flux_jobid_t id,
                     int priority,
                     uint32_t userid,
                     double t_submit,
                     const char *R,
                     void *arg)
{
    char *s;
    int rc = -1;
    struct simple_sched *ss = arg;

    struct rlist *alloc = rlist_from_R (R);
    if (!alloc) {
        flux_log_error (h, "hello: R=%s", R);
        return -1;
    }
    s = rlist_dumps (alloc);
    if ((rc = rlist_set_allocated (ss->rlist, alloc)) < 0)
        flux_log_error (h, "hello: rlist_remove (%s)", s);
    else
        flux_log (h, LOG_DEBUG, "hello: alloc %s", s);
    free (s);
    rlist_destroy (alloc);
    return 0;
}

static void status_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct simple_sched *ss = arg;
    struct rlist *rl = NULL;
    json_t *all = NULL;
    json_t *alloc = NULL;
    json_t *down = NULL;

    if (ss->rlist == NULL) {
        flux_respond_error (h, msg, EAGAIN, "sched-simple not initialized");
        return;
    }

    /*  Create list of all resources
     */
    if (!(rl = rlist_copy_empty (ss->rlist))
        || rlist_mark_up (rl, "all") < 0
        || !(all = rlist_to_R (rl))) {
        flux_log_error (h, "failed to create list of all resources");
        goto err;
    }
    rlist_destroy (rl);
    rl = NULL;

    /*  Create list of down resources
     */
    if (!(rl = rlist_copy_down (ss->rlist))
        || !(down = rlist_to_R (rl))) {
        flux_log_error (h, "failed to create list of down resources");
        goto err;
    }
    rlist_destroy (rl);
    rl = NULL;

    /*  Create list of allocated resources
     */
    if (!(rl = rlist_copy_allocated (ss->rlist))
        || !(alloc = rlist_to_R (rl))) {
        flux_log_error (h, "faile to create list of allocated resources");
        goto err;
    }
    rlist_destroy (rl);

    if (flux_respond_pack (h, msg, "{s:o s:o s:o}",
                           "all", all,
                           "allocated", alloc,
                           "down", down) < 0)
        flux_log_error (h, "flux_respond_pack");
    return;
err:
    rlist_destroy (rl);
    json_decref (all);
    json_decref (alloc);
    json_decref (down);
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "flux_respond_error");
}

static int ss_resource_update (struct simple_sched *ss, flux_future_t *f)
{
    const char *up = NULL;
    const char *down = NULL;
    const char *s;

    int rc = flux_rpc_get_unpack (f, "{s?s s?s}",
                                  "up", &up,
                                  "down", &down);
    if (rc < 0) {
        flux_log (ss->h, LOG_ERR, "unpacking acquire response failed");
        goto err;
    }

    flux_rpc_get (f, &s);
    flux_log (ss->h, LOG_INFO, "resource update: %s", s);

    /* Update resource states:
     */
    if ((up && rlist_mark_up (ss->rlist, up) < 0)
        || (down && rlist_mark_down (ss->rlist, down) < 0)) {
        flux_log_error (ss->h, "failed to update resource state");
        goto err;
    }
    rc = 0;
err:
    flux_future_reset (f);
    return rc;
}

static void acquire_continuation (flux_future_t *f, void *arg)
{
    struct simple_sched *ss = arg;
    if (flux_future_get (f, NULL) < 0) {
        flux_log (ss->h, LOG_ERR,
                  "exiting due to resource update failure: %s",
                  future_strerror (f, errno));
        flux_reactor_stop (flux_get_reactor (ss->h));
        return;
    }
    if (ss_resource_update (ss, f) == 0)
        try_alloc (ss->h, ss);
}

/*  Synchronously acquire resources from resource module.
 *  Configure internal resource state based on initial acquire response.
 */
static int ss_acquire_resources (flux_t *h, struct simple_sched *ss)
{
    int rc = -1;
    flux_future_t *f = NULL;
    json_t *o;
    char *by_rank = NULL;

    if (!(f = flux_rpc (h, "resource.acquire",
                        NULL,
                        FLUX_NODEID_ANY,
                        FLUX_RPC_STREAMING))) {
        flux_log_error (h, "rpc: resources.acquire");
        goto out;
    }
    ss->acquire_f = f;
    if (flux_rpc_get_unpack (f, "{s:o}", "resources", &o) < 0) {
        flux_log_error (h, "rpc_get (resource.acquire)");
        goto out;
    }
    if (!(by_rank = json_dumps (o, JSON_COMPACT))) {
        flux_log_error (h, "json_dumps (by_rank)");
        goto out;
    }
    if (!(ss->rlist = rlist_from_hwloc_by_rank (by_rank, ss->sched_pus))) {
        flux_log_error (h, "rank_list_create");
        goto out;
    }

    /* Update resource states:
     * - All resources down by default on first response
     */
    if (rlist_mark_down (ss->rlist, "all") < 0) {
        flux_log_error (h, "failed to set all discovered resources down");
        goto out;
    }

    if (ss_resource_update (ss, f) < 0) {
        flux_log_error (h, "failed to set initial resource state");
        goto out;
    }

    /* Add callback for multi-response acquire RPC
     */
    if (flux_future_then (f, -1., acquire_continuation, ss) < 0) {
        flux_log_error (h, "flux_future_then");
        goto out;
    }
    rc = 0;
out:
    free (by_rank);
    return rc;
}

static int simple_sched_init (flux_t *h, struct simple_sched *ss)
{
    int rc = -1;
    char *s = NULL;

    /*  Acquire resources from resource module and set initial
     *   resource state.
     */
    if (ss_acquire_resources (h, ss) < 0)
        goto out;

    /*  Complete synchronous hello protocol:
     */
    if (schedutil_hello (ss->util_ctx, hello_cb, ss) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto out;
    }
    if (schedutil_ready (ss->util_ctx,
                         ss->single ? "single": "unlimited",
                         NULL) < 0) {
        flux_log_error (h, "schedutil_ready");
        goto out;
    }
    s = rlist_dumps (ss->rlist);
    flux_log (h, LOG_DEBUG, "ready: %d of %d cores: %s",
                            ss->rlist->avail, ss->rlist->total, s);
    free (s);
    rc = 0;
out:
    return rc;
}

static char * get_alloc_mode (flux_t *h, const char *mode)
{
    if (strcmp (mode, "worst-fit") == 0
       || strcmp (mode, "first-fit") == 0
       || strcmp (mode, "best-fit") == 0)
        return strdup (mode);
    flux_log_error (h, "unknown allocation mode: %s\n", mode);
    return NULL;
}

static int process_args (flux_t *h, struct simple_sched *ss,
                         int argc, char *argv[])
{
    int i;
    for (i = 0; i < argc; i++) {
        if (strncmp ("mode=", argv[i], 5) == 0) {
            free (ss->mode);
            ss->mode = get_alloc_mode (h, argv[i]+5);
        }
        else if (strcmp ("unlimited", argv[i]) == 0) {
            ss->single = false;
        }
        else if (strcmp ("sched-PUs", argv[i]) == 0) {
            ss->sched_pus = true;
        }
        else {
            flux_log_error (h, "Unknown module option: '%s'", argv[i]);
            return -1;
        }
    }
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "*.resource-status", status_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    struct simple_sched *ss = NULL;
    flux_msg_handler_t **handlers = NULL;
    flux_reactor_t *r = flux_get_reactor (h);

    if (!(ss = simple_sched_create ())) {
        flux_log_error (h, "simple_sched_create");
        return -1;
    }

    if (process_args (h, ss, argc, argv) < 0)
        return -1;

    ss->util_ctx = schedutil_create (h, alloc_cb, free_cb, cancel_cb, ss);
    if (ss->util_ctx == NULL) {
        flux_log_error (h, "schedutil_create");
        goto done;
    }
    ss->h = h;
    ss->prep = flux_prepare_watcher_create (r, prep_cb, ss);
    ss->check = flux_check_watcher_create (r, check_cb, ss);
    ss->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!ss->prep || !ss->check || !ss->idle) {
        errno = ENOMEM;
        goto done;
    }
    flux_watcher_start (ss->prep);

    if (!(ss->queue = zlistx_new ()))
        goto done;
    zlistx_set_comparator (ss->queue, jobreq_cmp);
    zlistx_set_destructor (ss->queue, jobreq_destructor);

    if (simple_sched_init (h, ss) < 0)
        goto done;
    if (flux_msg_handler_addvec (h, htab, ss, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_add");
        goto done;
    }
    if (flux_reactor_run (r, 0) < 0)
        goto done;
    rc = 0;
done:
    simple_sched_destroy (h, ss);
    flux_msg_handler_delvec (handlers);
    return rc;
}

MOD_NAME ("sched-simple");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
