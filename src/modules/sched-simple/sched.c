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
    char *mode;             /* allocation mode */
    bool single;
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

static char *Rstring_create (struct rlist *l)
{
    char *s = NULL;
    json_t *R = rlist_to_R (l);
    if (R) {
        s = json_dumps (R, JSON_COMPACT);
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
    if (!job)
        return -1;
    jj = &job->jj;
    alloc = rlist_alloc (ss->rlist, ss->mode,
                         jj->nnodes, jj->nslots, jj->slot_size);
    if (!alloc) {
        const char *note = "unable to allocate provided jobspec";
        if (errno == ENOSPC)
            return rc;
        if (errno == EOVERFLOW)
            note = "unsatisfiable request";
        if (schedutil_alloc_respond_denied (ss->util_ctx,
                                            job->msg,
                                            note) < 0)
            flux_log_error (h, "schedutil_alloc_respond_denied");
        goto out;
    }
    s = rlist_dumps (alloc);
    if (!(R = Rstring_create (alloc)))
        flux_log_error (h, "Rstring_create");

    if (R && schedutil_alloc_respond_R (ss->util_ctx, job->msg, R, s) < 0)
        flux_log_error (h, "schedutil_alloc_respond_R");

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
        if (schedutil_alloc_respond_denied (ss->util_ctx,
                                            msg,
                                            job->jj.error) < 0)
            flux_log_error (h, "alloc_respond_denied");
        jobreq_destroy (job);
        return;
    }
    flux_log (h, LOG_DEBUG, "req: %ju: spec={%d,%d,%d}",
                            (uintmax_t) job->id, job->jj.nnodes,
                            job->jj.nslots, job->jj.slot_size);
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
                       const char *unused_arg1,
                       int unused_arg2,
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
    if ((rc = rlist_remove (ss->rlist, alloc)) < 0)
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
    json_t *o = NULL;

    if (ss->rlist == NULL) {
        flux_respond_error (h, msg, EAGAIN, "sched-simple not initialized");
        return;
    }
    if (!(o = rlist_to_R (ss->rlist))) {
        flux_log_error (h, "rlist_to_R_compressed");
        goto err;
    }
    if (flux_respond_pack (h, msg, "o", o) < 0)
        flux_log_error (h, "flux_respond_pack");
    return;
err:
    json_decref (o);
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "flux_respond_error");
}

static int simple_sched_init (flux_t *h, struct simple_sched *ss)
{
    int rc = -1;
    char *s = NULL;
    flux_future_t *f = NULL;
    const char *by_rank = NULL;

    /* synchronously lookup by_rank for initialization */
    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_WAITCREATE,
                                  "resource.hwloc.by_rank"))) {
        flux_log_error (h, "lookup resource.hwloc.by_rank");
        goto out;
    }
    if (flux_kvs_lookup_get (f, &by_rank) < 0) {
        flux_log_error (h, "kvs_lookup_get (resource.hwloc.by_rank)");
        goto out;
    }
    if (!(ss->rlist = rlist_from_hwloc_by_rank (by_rank))) {
        flux_log_error (h, "rank_list_create");
        goto out;
    }
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
    flux_future_destroy (f);
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
        else {
            flux_log_error (h, "Unknown module option: '%s'", argv[i]);
            return -1;
        }
    }
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "sched-simple.status", status_cb, FLUX_ROLE_USER },
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
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
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
