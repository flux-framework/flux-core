/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* runjob.c - fastpath for running a job in one RPC
 *
 * The RPC returns when the job is inactive and includes the wait status.
 *
 * Access is restricted to instance owner only.
 * Job ID is issued and jobspec is constructed here, instead of in job-ingest.
 * Jobspec validators are bypassed.
 * Jobspec is not signed, nor is signed jobspec stored to KVS.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libjob/specutil.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libutil/errno_safe.h"

#include "job.h"
#include "wait.h"
#include "event.h"
#include "runjob.h"

struct runjob {
    struct job_manager *ctx;
    struct fluid_generator fluid_gen;
};

static void jobspec_continuation (flux_future_t *f, void *arg)
{
    struct runjob *runjob = arg;
    flux_t *h = flux_future_get_flux (f);
    struct job *job = flux_future_aux_get (f, "job");
    const char *errstr = NULL;

    if (flux_rpc_get (f, NULL) < 0) {
        errstr = "eventlog commit failed";
        goto error;
    }
    if (event_job_post_pack (runjob->ctx->event,
                             job,
                             "submit",
                             0,
                             "{s:i s:i s:i}",
                             "userid", job->userid,
                             "urgency", job->urgency,
                             "flags", job->flags) < 0) {
        errstr = "error posting submit event";
        goto error;
    }
    wait_notify_active (runjob->ctx->wait, job);
    job_aux_delete (job, f);
    return;
error:
    if (flux_respond_error (h, wait_get_waiter (job), errno, errstr) < 0)
        flux_log_error (h, "error responding to runjob");
    zhashx_delete (runjob->ctx->active_jobs, &job->id);
}

static flux_future_t *commit_jobspec (flux_t *h, struct job *job)
{
    char key[64];
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn;

    if (flux_job_kvs_key (key, sizeof (key), job->id, "jobspec") < 0)
        return NULL;
    if (!(txn = flux_kvs_txn_create ()))
        return NULL;
    if (flux_kvs_txn_pack (txn, 0, key, "O", job->jobspec_redacted) < 0)
        goto error;
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        goto error;
    flux_kvs_txn_destroy (txn);
    return f;
error:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return NULL;
}

void runjob_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct job_manager *ctx = arg;
    struct runjob *runjob = ctx->runjob;
    json_t *command;
    json_t *attributes;
    struct resource_param param = { .nodes = 0 };
    char errbuf[128];
    const char *errstr = NULL;
    struct job *job = NULL;
    const char *envkey = "attributes.system.environment";
    flux_future_t *f;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:o s:i s:i s:i}",
                             "command", &command,
                             "attributes", &attributes,
                             "ntasks", &param.ntasks,
                             "cores-per-task", &param.cores_per_task,
                             "gpus-per-task", &param.gpus_per_task) < 0) {
        errstr = "malformed runjob request";
        goto error;
    }
    if (!(job = job_create ()))
        goto error;
    /* The runjob request will be handled as if it were a 'wait' request
     * for this job.  Code in wait.c responds to the request once the job
     * becomes inactive.
     */
    job->flags = FLUX_JOB_WAITABLE;
    if (wait_set_waiter (runjob->ctx->wait, job, msg) < 0)
        goto error;
    if (flux_msg_get_userid (msg, &job->userid) < 0)
        goto error;
    if (fluid_generate (&runjob->fluid_gen, &job->id) < 0) {
        errstr = "error generating job id";
        errno = EINVAL;
        goto error;
    }
    /* The redacted jobspec is not actually redacted until it (in full)
     * becomes part of the KVS transaction below.
     */
    if (!(job->jobspec_redacted  = specutil_jobspec_create (attributes,
                                                            command,
                                                            &param,
                                                            errbuf,
                                                            sizeof (errbuf)))) {
        errstr = errbuf;
        goto error;
    }
    /* Start KVS commit of jobspec.
     * If the commit is successful, its continuation posts the submit event
     * which kicks the job state machine.
     * N.B. future 'f' destruction is tied to 'job', not the other way around
     */
    if (!(f = commit_jobspec (h, job))
        || flux_future_aux_set (f, "job", job, NULL) < 0
        || flux_future_then (f, -1, jobspec_continuation, runjob) < 0
        || job_aux_set (job, NULL, f, (flux_free_f)flux_future_destroy) < 0) {
        flux_future_destroy (f);
        errstr = "error committing jobspec to KVS";
        goto error;
    }
    specutil_attr_del (job->jobspec_redacted, envkey); // redact environment
    zhashx_update (ctx->active_jobs, &job->id, job);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error resopnding to runjob");
    job_decref (job);
}

void runjob_ctx_destroy (struct runjob *runjob)
{
    if (runjob) {
        int saved_errno = errno;
        free (runjob);
        errno = saved_errno;
    }
}

struct runjob *runjob_ctx_create (struct job_manager *ctx)
{
    struct runjob *runjob;

    if (!(runjob = calloc (1, sizeof (*runjob))))
        return NULL;
    runjob->ctx = ctx;
    if (fluid_init (&runjob->fluid_gen,
                    16383, // reserved by job-ingest
                    fluid_get_timestamp (ctx->max_jobid)) < 0) {
        flux_log (ctx->h, LOG_ERR, "fluid_init failed");
        goto error;
    }
    return runjob;
error:
    runjob_ctx_destroy (runjob);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
