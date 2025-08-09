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
#include <flux/core.h>
#include <flux/schedutil.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/job.h"
#include "src/common/libjob/jj.h"
#include "src/common/libjob/idf58.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

// e.g. flux module debug --setbit 0x1 sched-simple
// e.g. flux module debug --clearbit 0x1 sched-simple
enum module_debug_flags {
    DEBUG_FAIL_ALLOC = 1, // while set, alloc requests fail
    DEBUG_ANNOTATE_REASON_PENDING = 2, // add reason_pending annotation
    DEBUG_EXPIRATION_UPDATE_DENY = 4,  // deny sched.expiration RPCs
};

struct jobreq {
    void *handle;
    const flux_msg_t *msg;
    uint32_t uid;
    unsigned int priority;
    double t_submit;
    flux_jobid_t id;
    struct jj_counts jj;
    json_t *constraints;
    int errnum;
};

struct simple_sched {
    flux_t *h;
    flux_future_t *acquire_f; /* resource.acquire future */

    char *alloc_mode;             /* allocation mode */
    char *mode;             /* concurrency mode */
    unsigned int alloc_limit; /* 0 = unlimited */
    int schedutil_flags;
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
        json_decref (job->constraints);
        ERRNO_SAFE_WRAP (free, job);
    }
}

static void jobreq_destructor (void **x)
{
    if (x) {
        jobreq_destroy (*x);
        *x = NULL;
    }
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Taken from modules/job-manager/job.c */
static int jobreq_cmp (const void *x, const void *y)
{
    const struct jobreq *j1 = x;
    const struct jobreq *j2 = y;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
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
jobreq_create (const flux_msg_t *msg)
{
    struct jobreq *job = calloc (1, sizeof (*job));
    json_t *jobspec;

    if (job == NULL)
        return NULL;

    if (flux_msg_unpack (msg,
                         "{s:I s:i s:i s:f s:o}",
                         "id", &job->id,
                         "priority", &job->priority,
                         "userid", &job->uid,
                         "t_submit", &job->t_submit,
                         "jobspec", &jobspec) < 0)
        goto err;
    job->msg = flux_msg_incref (msg);
    if (jj_get_counts_json (jobspec, &job->jj) < 0)
        job->errnum = errno;
    else if (job->jj.slot_gpus > 0) {
        snprintf (job->jj.error,
                  sizeof (job->jj.error),
                  "sched-simple does not support resource type 'gpu'");
        errno = EINVAL;
        job->errnum = errno;
    }
    if (json_unpack (jobspec,
                     "{s:{s?{s?O}}}",
                     "attributes",
                       "system",
                         "constraints", &job->constraints) < 0) {
        job->errnum = errno;
        goto err;
    }

    return job;
err:
    jobreq_destroy (job);
    return NULL;
}

static void simple_sched_destroy (flux_t *h, struct simple_sched *ss)
{
    if (ss) {
        int saved_errno = errno;
        if (ss->queue) {
            struct jobreq *job = zlistx_first (ss->queue);
            while (job) {
                flux_respond_error (h,
                                    job->msg,
                                    ENOSYS,
                                    "simple sched exiting");
                job = zlistx_next (ss->queue);
            }
            zlistx_destroy (&ss->queue);
        }
        flux_future_destroy (ss->acquire_f);
        flux_watcher_destroy (ss->prep);
        flux_watcher_destroy (ss->check);
        flux_watcher_destroy (ss->idle);
        schedutil_destroy (ss->util_ctx);
        rlist_destroy (ss->rlist);
        free (ss->alloc_mode);
        free (ss->mode);
        free (ss);
        errno = saved_errno;
    }
}

static struct simple_sched * simple_sched_create (void)
{
    struct simple_sched *ss = calloc (1, sizeof (*ss));
    if (ss == NULL)
        return NULL;

    /* default limit to 8, testing shows quite good throughput without
     * concurrency being excessively large.
     */
    ss->alloc_limit = 8;

    ss->schedutil_flags = SCHEDUTIL_HELLO_PARTIAL_OK;
    return ss;
}

static char *Rstring_create (struct simple_sched *ss,
                             struct rlist *l,
                             double now,
                             double timelimit)
{
    char *s = NULL;
    json_t *R = NULL;
    l->starttime = now;
    l->expiration = 0.;
    if (timelimit > 0.) {
        l->expiration = now + timelimit;
    }
    else if (ss->rlist->expiration > 0.) {
        l->expiration = ss->rlist->expiration;
    }
    if ((R = rlist_to_R (l))) {
        s = json_dumps (R, JSON_COMPACT);
        json_decref (R);
    }
    return s;
}

static struct rlist *sched_alloc (struct simple_sched *ss,
                                  struct jobreq *job,
                                  flux_error_t *errp)
{
    struct rlist_alloc_info ai = {
        .mode = ss->alloc_mode,
        .nnodes = job->jj.nnodes,
        .nslots = job->jj.nslots,
        .slot_size = job->jj.slot_size,
        .exclusive = job->jj.exclusive,
        .constraints = job->constraints
    };
    return rlist_alloc (ss->rlist, &ai, errp);
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
    bool fail_alloc = flux_module_debug_test (h, DEBUG_FAIL_ALLOC, false);
    flux_error_t error;

    if (!job)
        return -1;

    jj = &job->jj;
    if (!fail_alloc) {
        errno = 0;
        alloc = sched_alloc (ss, job, &error);
    }
    if (!alloc || !(R = Rstring_create (ss, alloc, now, jj->duration))) {
        const char *note = "unable to allocate provided jobspec";
        if (alloc != NULL) {
            /*  unlikely: allocation succeeded but Rstring_create failed */
            note = "internal scheduler error generating R";
            flux_log (ss->h, LOG_ERR, "%s", note);
            if (rlist_free (ss->rlist, alloc) < 0)
                flux_log_error (h, "try_alloc: rlist_free");
            rlist_destroy (alloc);
            alloc = NULL;
        }
        else if (errno == ENOSPC)
            return rc;
        else if (errno == EOVERFLOW)
            note = "unsatisfiable request";
        else if (fail_alloc)
            note = "DEBUG_FAIL_ALLOC";
        if (schedutil_alloc_respond_deny (ss->util_ctx, job->msg, note) < 0)
            flux_log_error (h, "schedutil_alloc_respond_deny");
        goto out;
    }
    s = rlist_dumps (alloc);

    if (schedutil_alloc_respond_success_pack (ss->util_ctx,
                                              job->msg,
                                              R,
                                              "{ s:{s:s s:n s:n} }",
                                              "sched",
                                                "resource_summary", s,
                                                "reason_pending",
                                                "jobs_ahead") < 0)
        flux_log_error (h, "schedutil_alloc_respond_success_pack");

    flux_log (h, LOG_DEBUG, "alloc: %s: %s", idf58 (job->id), s);
    rc = 0;

out:
    zlistx_delete (ss->queue, job->handle);
    rlist_destroy (alloc);
    free (R);
    free (s);
    return rc;
}

static void annotate_reason_pending (struct simple_sched *ss)
{
    int jobs_ahead = 0;

    if (!flux_module_debug_test (ss->h, DEBUG_ANNOTATE_REASON_PENDING, false))
        return;

    struct jobreq *job = zlistx_first (ss->queue);
    while (job) {
        if (schedutil_alloc_respond_annotate_pack (ss->util_ctx,
                                                   job->msg,
                                                   "{ s:{s:s s:i} }",
                                                   "sched",
                                                   "reason_pending",
                                                     "insufficient resources",
                                                   "jobs_ahead",
                                                     jobs_ahead++) < 0)
            flux_log_error (ss->h, "schedutil_alloc_respond_annotate_pack");
        job = zlistx_next (ss->queue);
    }
}

static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct simple_sched *ss = arg;
    /* if there is at least one job to schedule, start check and idle */
    if (zlistx_size (ss->queue) > 0) {
        /* If there's a new job to process, start idle watcher */
        flux_watcher_start (ss->check);
        flux_watcher_start (ss->idle);
    }
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct simple_sched *ss = arg;
    flux_watcher_stop (ss->idle);

    /* See if we can fulfill alloc for a pending job
     * If current head of queue can't be allocated, stop the prep
     *  watcher, i.e. block. O/w, retry on next loop.
     */
    if (try_alloc (ss->h, ss) < 0 && errno == ENOSPC) {
        annotate_reason_pending (ss);
        flux_watcher_stop (ss->prep);
        flux_watcher_stop (ss->check);
    }
}

static int try_free (flux_t *h,
                     struct simple_sched *ss,
                     flux_jobid_t id,
                     json_t *R,
                     bool final)
{
    int rc = -1;
    char *r = NULL;
    json_error_t error;
    struct rlist *alloc = rlist_from_json (R, &error);
    if (!alloc) {
        char *s = json_dumps (R, JSON_COMPACT);
        flux_log_error (h, "free: unable to parse R=%s: %s", s, error.text);
        ERRNO_SAFE_WRAP (free, s);
        return -1;
    }
    r = rlist_dumps (alloc);
    if ((rc = rlist_free_tolerant (ss->rlist, alloc)) < 0)
        flux_log_error (h, "free: %s", r);
    else {
        flux_log (h,
                  LOG_DEBUG,
                  "free: %s %s%s",
                  r,
                  idf58 (id),
                  final ? " (final)" : "");
    }
    free (r);
    rlist_destroy (alloc);
    return rc;
}

void free_cb (flux_t *h, const flux_msg_t *msg, const char *R_str, void *arg)
{
    struct simple_sched *ss = arg;
    json_t *R;
    flux_jobid_t id;
    int final = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:o s?b}",
                             "id", &id,
                             "R", &R,
                             "final", &final) < 0) {
        flux_log (h, LOG_ERR, "free: error unpacking sched.free request");
        return;
    }

    if (try_free (h, ss, id, R, final) < 0) {
        flux_log_error (h, "free: could not free R. Stopping scheduler.");
        /* Make this error fatal to the scheduler so that tests will fail.
         */
        flux_reactor_stop_error (flux_get_reactor (h));
        return;
    }
    /* This is a no-op now that sched.free requires no response
     * but we still call it to get test coverage.
     */
    if (schedutil_free_respond (ss->util_ctx, msg) < 0)
        flux_log_error (h, "free_cb: schedutil_free_respond");

    /* See if we can fulfill alloc for a pending job */
    flux_watcher_start (ss->prep);
}

static void alloc_cb (flux_t *h, const flux_msg_t *msg, void *arg)
{
    struct simple_sched *ss = arg;
    struct jobreq *job;
    bool search_dir;

    if (ss->alloc_limit
        && zlistx_size (ss->queue) >= ss->alloc_limit) {
        flux_log (h,
                  LOG_ERR,
                  "alloc received above max concurrency: %d",
                  ss->alloc_limit);
        errno = EINVAL;
        goto err;
    }
    if (!(job = jobreq_create (msg))) {
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

    flux_log (h,
              LOG_DEBUG,
              "req: %s: spec={%d,%d,%d} duration=%.1f",
              idf58 (job->id),
              job->jj.nnodes,
              job->jj.nslots,
              job->jj.slot_size,
              job->jj.duration);

    search_dir = job->priority > FLUX_JOB_URGENCY_DEFAULT;
    job->handle = zlistx_insert (ss->queue, job, search_dir);
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
static void cancel_cb (flux_t *h, const flux_msg_t *msg, void *arg)
{
    struct simple_sched *ss = arg;
    flux_jobid_t id;
    struct jobreq *job;

    if (flux_msg_unpack (msg, "{s:I}", "id", &id) < 0) {
        flux_log_error (h, "invalid sched.cancel request");
        return;
    }

    if ((job = jobreq_find (ss, id))) {
        if (schedutil_alloc_respond_cancel (ss->util_ctx, job->msg) < 0) {
            flux_log_error (h, "alloc_respond_cancel");
            return;
        }
        zlistx_delete (ss->queue, job->handle);
        annotate_reason_pending (ss);
    }
}

/* Job manager indicates there is a priority change to a job.  If a
 * matching job found in queue, update the priority and reorder queue
 * as necessary.
 */
static void prioritize_cb (flux_t *h, const flux_msg_t *msg, void *arg)
{
    static int min_sort_size = 4;
    struct simple_sched *ss = arg;
    struct jobreq *job;
    json_t *jobs;
    size_t index;
    json_t *arr;
    size_t count;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0)
        goto proto_error;

    count = json_array_size (jobs);
    json_array_foreach (jobs, index, arr) {
        flux_jobid_t id;
        int64_t priority;

        if (json_unpack (arr, "[I,I]", &id, &priority) < 0)
            goto proto_error;

        if ((job = jobreq_find (ss, id))) {
            job->priority = priority;
            if (count < min_sort_size)
                zlistx_reorder (ss->queue, job->handle, true);
        }
    }
    if (count >= min_sort_size) {
        zlistx_sort (ss->queue);

        /*  zlistx handles are invalidated after a zlistx_sort(),
         *   so reacquire them now
         */
        job = zlistx_first (ss->queue);
        while (job) {
            job->handle = zlistx_cursor (ss->queue);
            job = zlistx_next (ss->queue);
        }
    }
    annotate_reason_pending (ss);
    return;

proto_error:
    flux_log (h, LOG_ERR, "malformed sched.reprioritize request");
    return;
}

static int hello_cb (flux_t *h,
                     const flux_msg_t *msg,
                     const char *R,
                     void *arg)
{
    char *s;
    int rc = -1;
    struct simple_sched *ss = arg;
    struct rlist *alloc;
    flux_jobid_t id;
    unsigned int priority;
    uint32_t userid;
    double t_submit;
    const char *free_ranks = NULL;

    if (flux_msg_unpack (msg,
                         "{s:I s:i s:i s:f s?s}",
                         "id", &id,
                         "priority", &priority,
                         "userid", &userid,
                         "t_submit", &t_submit,
                         "free", &free_ranks) < 0) {
        flux_log_error (h, "hello: invalid hello payload");
        return -1;
    }

    flux_log (h,
              LOG_DEBUG,
              "hello: id=%s priority=%u userid=%u t_submit=%0.1f %s%s",
              idf58 (id),
              priority,
              (unsigned int)userid,
              t_submit,
              free_ranks ? "free=" : "",
              free_ranks ? free_ranks : "");

    alloc = rlist_from_R (R);
    if (!alloc) {
        flux_log_error (h, "hello: R=%s", R);
        return -1;
    }
    s = rlist_dumps (alloc);
    if ((rc = rlist_set_allocated (ss->rlist, alloc)) < 0)
        flux_log_error (h, "hello: alloc %s", s);
    else
        flux_log (h, LOG_DEBUG, "hello: alloc %s", s);
    free (s);
    rlist_destroy (alloc);
    return rc;
}

static void status_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct simple_sched *ss = arg;
    struct rlist *rl = NULL;
    json_t *all = NULL;
    json_t *alloc = NULL;
    json_t *down = NULL;

    /* N.B. no need to check if ss->rlist is set.  The reactor is not
     * run until after synchronous initialization in
     * simple_sched_init() is complete. */

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
        flux_log_error (h, "failed to create list of allocated resources");
        goto err;
    }
    rlist_destroy (rl);

    if (flux_respond_pack (h,
                           msg,
                           "{s:o s:o s:o}",
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

static void feasibility_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct simple_sched *ss = arg;
    struct jj_counts jj;
    json_t *jobspec;
    json_t *constraints = NULL;
    struct rlist *alloc = NULL;
    const char *errmsg = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o}",
                            "jobspec", &jobspec) < 0)
        goto err;
    if (json_unpack (jobspec,
                     "{s:{s?{s?o}}}",
                     "attributes",
                       "system",
                         "constraints", &constraints) < 0)
        goto err;

    if (jj_get_counts_json (jobspec, &jj) < 0) {
        errmsg = jj.error;
        goto err;
    }
    if (jj.slot_gpus > 0) {
        errno = EINVAL;
        errmsg = "Unsupported resource type 'gpu'";
        goto err;
    }

    struct rlist_alloc_info ai = {
        .mode = ss->alloc_mode,
        .nnodes = jj.nnodes,
        .nslots = jj.nslots,
        .slot_size = jj.slot_size,
        .constraints = constraints
    };
    if (!(alloc = rlist_alloc (ss->rlist, &ai, &error))) {
        if (errno != ENOSPC) {
            errmsg = error.text;
            goto err;
        }
        /* Fall-through: if ENOSPC then job is satisfiable */
    }
    if (alloc && rlist_free (ss->rlist, alloc) < 0) {
        /*  If rlist_free() fails we're in trouble because
         *  ss->rlist will have an invalid allocation. This should
         *  be rare if not impossible, so just exit the reactor.
         *
         *  The sched module can then be reloaded without loss of jobs.
         */
        flux_log_error (h, "feasibility_cb: failed to free fake alloc");
        flux_reactor_stop_error (flux_get_reactor (h));
        errmsg = "Internal scheduler error";
        goto err;
    }
    rlist_destroy (alloc);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "feasibility_cb: flux_respond_pack");
    return;
err:
    rlist_destroy (alloc);
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "feasibility_cb: flux_respond_error");
}

/* For testing purposes, support the sched.expiration RPC even though
 * sched-simple is not a planning scheduler.
 */
static void expiration_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct simple_sched *ss = arg;
    flux_jobid_t id;
    double expiration;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:F}",
                             "id", &id,
                             "expiration", &expiration) < 0)
        goto err;
    if (expiration < 0.) {
        errno = EINVAL;
        goto err;
    }
    if (flux_module_debug_test (ss->h, DEBUG_EXPIRATION_UPDATE_DENY, false)) {
        errmsg = "Rejecting expiration update for testing";
        goto err;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "feasibility_cb: flux_respond_pack");
    return;
err:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "expiration_cb: flux_respond_error");
}

static int ss_resource_update (struct simple_sched *ss, flux_future_t *f)
{
    const char *up = NULL;
    const char *down = NULL;
    const char *shrink = NULL;
    double expiration = -1.;
    const char *s;

    int rc = flux_rpc_get_unpack (f,
                                  "{s?s s?s s?s s?F}",
                                  "up", &up,
                                  "down", &down,
                                  "shrink", &shrink,
                                  "expiration", &expiration);
    if (rc < 0) {
        flux_log (ss->h, LOG_ERR, "unpacking acquire response failed");
        goto err;
    }

    flux_rpc_get (f, &s);
    flux_log (ss->h, LOG_DEBUG, "resource update: %s", s);

    /* Update resource states:
     */
    if ((up && rlist_mark_up (ss->rlist, up) < 0)
        || (down && rlist_mark_down (ss->rlist, down) < 0)) {
        flux_log_error (ss->h, "failed to update resource state");
        goto err;
    }

    /* Handle shrink targets:
     */
    if (shrink) {
        struct idset *ids = NULL;
        if (!(ids = idset_decode (shrink))
            || (rc = rlist_remove_ranks (ss->rlist, ids)) < 0)
            flux_log_error (ss->h, "failed to shrink resource set");
        idset_destroy (ids);
        if (rc < 0)
            goto err;
    }

    if (expiration >= 0. && ss->rlist->expiration != expiration) {
        flux_log (ss->h,
                  LOG_INFO,
                  "resource expiration updated to %.2f",
                  expiration);
        ss->rlist->expiration = expiration;
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
        flux_log (ss->h,
                  LOG_ERR,
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
    json_t *R;
    json_error_t e;

    if (!(f = flux_rpc (h,
                        "resource.acquire",
                        NULL,
                        FLUX_NODEID_ANY,
                        FLUX_RPC_STREAMING))) {
        flux_log_error (h, "rpc: resources.acquire");
        goto out;
    }
    ss->acquire_f = f;
    if (flux_rpc_get_unpack (f, "{s:o}", "resources", &R) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "resource.acquire failed: %s",
                  future_strerror (f, errno));
        goto out;
    }
    if (!(ss->rlist = rlist_from_json (R, &e))) {
        flux_log_error (h, "rlist_from_json: %s", e.text);
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
    return rc;
}

static int simple_sched_init (flux_t *h, struct simple_sched *ss)
{
    int rc = -1;
    char *s = NULL;
    flux_future_t *f = NULL;

    /*  Per RFC 27 register 'feasibility' service for feasibility.check RPC
     */
    if (!(f = flux_service_register (h, "feasibility"))
        || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "Failed to register feasibility service");
        goto out;
    }

    /*  Acquire resources from resource module and set initial
     *   resource state.
     */
    if (ss_acquire_resources (h, ss) < 0)
        goto out;

    /*  Complete synchronous hello protocol:
     */
    if (schedutil_hello (ss->util_ctx) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto out;
    }
    if (schedutil_ready (ss->util_ctx,
                         ss->mode ? ss->mode : "limited=8",
                         NULL) < 0) {
        flux_log_error (h, "schedutil_ready");
        goto out;
    }
    s = rlist_dumps (ss->rlist);
    flux_log (h,
              LOG_DEBUG,
              "ready: %d of %d cores: %s",
              ss->rlist->avail,
              ss->rlist->total,
              s);
    free (s);
    rc = 0;
out:
    flux_future_destroy (f);
    return rc;
}

static char * get_alloc_mode (flux_t *h, const char *alloc_mode)
{
    if (streq (alloc_mode, "worst-fit")
        || streq (alloc_mode, "first-fit")
        || streq (alloc_mode, "best-fit"))
        return strdup (alloc_mode);
    flux_log (h, LOG_ERR, "unknown allocation mode: %s", alloc_mode);
    return NULL;
}

static void set_mode (struct simple_sched *ss, const char *mode)
{
    if (strstarts (mode, "limited=")) {
        char *endptr;
        int n = strtol (mode+8, &endptr, 0);
        if (*endptr != '\0' || n <= 0) {
            flux_log (ss->h, LOG_ERR, "invalid limited value: %s\n", mode);
            return;
        }
        ss->alloc_limit = n;
    }
    else if (strcasecmp (mode, "unlimited") == 0) {
        ss->alloc_limit = 0;
    }
    else {
        flux_log (ss->h, LOG_ERR, "unknown mode: %s", mode);
        return;
    }
    free (ss->mode);
    if (!(ss->mode = strdup (mode)))
        flux_log_error (ss->h, "error setting mode: %s", mode);
}

static struct schedutil_ops ops = {
    .hello = hello_cb,
    .alloc = alloc_cb,
    .free = free_cb,
    .cancel = cancel_cb,
    .prioritize = prioritize_cb,
};

static int process_args (flux_t *h, struct simple_sched *ss,
                         int argc, char *argv[])
{
    int i;
    for (i = 0; i < argc; i++) {
        if (strstarts (argv[i], "alloc-mode=")) {
            free (ss->alloc_mode);
            ss->alloc_mode = get_alloc_mode (h, argv[i]+11);
        }
        else if (strstarts (argv[i], "mode=")) {
            set_mode (ss, argv[i]+5);
        }
        else if (streq (argv[i], "test-free-nolookup")) {
            ss->schedutil_flags |= SCHEDUTIL_FREE_NOLOOKUP;
        }
        else if (streq (argv[i], "test-hello-nopartial")) {
            ss->schedutil_flags &= ~SCHEDUTIL_HELLO_PARTIAL_OK;
        }
        else {
            flux_log (h, LOG_ERR, "Unknown module option: '%s'", argv[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}



static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "*.resource-status", status_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "*.expiration", expiration_cb, FLUX_ROLE_OWNER },
    { FLUX_MSGTYPE_REQUEST,
      "feasibility.check",
      feasibility_cb,
      FLUX_ROLE_USER
    },
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

    ss->util_ctx = schedutil_create (h, ss->schedutil_flags, &ops, ss);
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

    /* Let `flux module load simple-sched` return before synchronous
     * initialization with resource and job-manager modules.
     */
    if (flux_module_set_running (h) < 0)
        goto done;

    if (simple_sched_init (h, ss) < 0)
        goto done;
    /* N.B. simple_sched_init() calls schedutil_create(),
     * which registers the "sched" service  name
     */
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
