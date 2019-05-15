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

#include "src/common/libschedutil/schedutil.h"
#include "libjj.h"
#include "rlist.h"

struct jobreq {
    flux_msg_t *msg;
    flux_jobid_t id;
    struct jj_counts jj;
    int errnum;
};

struct simple_sched {
    char *mode;          /* allocation mode */
    struct rlist *rlist; /* list of resources */
    struct jobreq *job;  /* currently processed job */
    struct ops_context *ops;
};

static void jobreq_destroy (struct jobreq *job)
{
    if (job) {
        flux_msg_destroy (job->msg);
        free (job);
    }
}

static struct jobreq *jobreq_create (const flux_msg_t *msg, const char *jobspec)
{
    struct jobreq *job = calloc (1, sizeof (*job));
    int pri;
    uint32_t uid;
    double t_submit;

    if (job == NULL)
        return NULL;
    if (schedutil_alloc_request_decode (msg, &job->id, &pri, &uid, &t_submit) < 0)
        goto err;
    if (!(job->msg = flux_msg_copy (msg, true)))
        goto err;
    if (libjj_get_counts (jobspec, &job->jj) < 0)
        job->errnum = errno;
    return job;
err:
    jobreq_destroy (job);
    return NULL;
}

static void simple_sched_destroy (flux_t *h, struct simple_sched *ss)
{
    schedutil_ops_unregister (ss->ops);
    if (ss->job) {
        flux_respond_error (h, ss->job->msg, ENOSYS, "simple sched exiting");
        jobreq_destroy (ss->job);
    }
    rlist_destroy (ss->rlist);
    free (ss->mode);
    free (ss);
}

static struct simple_sched *simple_sched_create (void)
{
    struct simple_sched *ss = calloc (1, sizeof (*ss));
    if (ss == NULL)
        return NULL;
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

static void try_alloc (flux_t *h, struct simple_sched *ss)
{
    char *s = NULL;
    struct rlist *alloc = NULL;
    struct jj_counts *jj = NULL;
    char *R = NULL;
    if (!ss->job)
        return;
    jj = &ss->job->jj;
    alloc = rlist_alloc (ss->rlist, ss->mode, jj->nnodes, jj->nslots, jj->slot_size);
    if (!alloc) {
        const char *note = "unable to allocate provided jobspec";
        if (errno == ENOSPC)
            return;
        if (errno == EOVERFLOW)
            note = "unsatisfiable request";
        if (schedutil_alloc_respond_denied (h, ss->job->msg, note) < 0)
            flux_log_error (h, "schedutil_alloc_respond_denied");
        goto out;
    }
    s = rlist_dumps (alloc);
    if (!(R = Rstring_create (alloc)))
        flux_log_error (h, "Rstring_create");

    if (R && schedutil_alloc_respond_R (h, ss->job->msg, R, s) < 0)
        flux_log_error (h, "schedutil_alloc_respond_R");

    flux_log (h, LOG_DEBUG, "alloc: %ju: %s", (uintmax_t)ss->job->id, s);

out:
    jobreq_destroy (ss->job);
    ss->job = NULL;
    rlist_destroy (alloc);
    free (R);
    free (s);
}

void exception_cb (flux_t *h,
                   flux_jobid_t id,
                   const char *type,
                   int severity,
                   void *arg)
{
    char note[80];
    struct simple_sched *ss = arg;
    if (ss->job == NULL || ss->job->id != id || severity > 0)
        return;
    flux_log (h, LOG_DEBUG, "alloc aborted: id=%ju", (uintmax_t)id);
    snprintf (note, sizeof (note) - 1, "alloc aborted due to exception");
    if (schedutil_alloc_respond_denied (h, ss->job->msg, note) < 0)
        flux_log_error (h, "alloc_respond_denied");
    jobreq_destroy (ss->job);
    ss->job = NULL;
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
    if (schedutil_free_respond (h, msg) < 0)
        flux_log_error (h, "free_cb: schedutil_free_respond");

    /* See if we can fulfill alloc for a pending job */
    try_alloc (h, ss);
}

static void alloc_cb (flux_t *h, const flux_msg_t *msg, const char *jobspec, void *arg)
{
    struct simple_sched *ss = arg;

    if (ss->job) {
        flux_log (h, LOG_ERR, "alloc received before previous one handled");
        goto err;
    }
    if (!(ss->job = jobreq_create (msg, jobspec))) {
        flux_log_error (h, "alloc: jobreq_create");
        goto err;
    }
    if (ss->job->errnum != 0) {
        if (schedutil_alloc_respond_denied (h, msg, ss->job->jj.error) < 0)
            flux_log_error (h, "alloc_respond_denied");
        jobreq_destroy (ss->job);
        ss->job = NULL;
        return;
    }
    flux_log (h,
              LOG_DEBUG,
              "req: %ju: spec={%d,%d,%d}",
              (uintmax_t)ss->job->id,
              ss->job->jj.nnodes,
              ss->job->jj.nslots,
              ss->job->jj.slot_size);
    try_alloc (h, ss);
    return;
err:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "alloc: flux_respond_error");
}

static int hello_cb (flux_t *h, const char *R, void *arg)
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

static void status_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
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
    if (!(f = flux_kvs_lookup (h,
                               NULL,
                               FLUX_KVS_WAITCREATE,
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
    if (schedutil_hello (h, hello_cb, ss) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto out;
    }
    if (schedutil_ready (h, "single", NULL) < 0) {
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

static char *get_alloc_mode (flux_t *h, const char *mode)
{
    if (strcmp (mode, "worst-fit") == 0 || strcmp (mode, "first-fit") == 0
        || strcmp (mode, "best-fit") == 0)
        return strdup (mode);
    flux_log_error (h, "unknown allocation mode: %s\n", mode);
    return NULL;
}

static int process_args (flux_t *h, struct simple_sched *ss, int argc, char *argv[])
{
    int i;
    for (i = 0; i < argc; i++) {
        if (strncmp ("mode=", argv[i], 5) == 0) {
            free (ss->mode);
            ss->mode = get_alloc_mode (h, argv[i] + 5);
        } else {
            flux_log_error (h, "Unknown module option: '%s'", argv[i]);
            return -1;
        }
    }
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    {FLUX_MSGTYPE_REQUEST, "sched-simple.status", status_cb, FLUX_ROLE_USER},
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

    ss->ops = schedutil_ops_register (h, alloc_cb, free_cb, exception_cb, ss);
    if (!(ss->ops)) {
        flux_log_error (h, "schedutil_ops_register");
        goto done;
    }
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
