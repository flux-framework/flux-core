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
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <jansson.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/context.h>
#include <flux/security/sign.h>
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libfluxutil/policy.h"
#include "ccan/str/str.h"

#include "util.h"
#include "job.h"
#include "pipeline.h"

/* job-ingest takes in signed jobspec submitted through flux_job_submit(),
 * performing the following tasks for each job:
 *
 * 1) verify that submitting userid == userid that signed jobspec
 * 2) verify that enclosed jobspec is valid per RFC 14
 * 3) assign jobid using distributed 64-bit FLUID generator
 * 4) commit job data to KVS per RFC 16 (KVS Job Schema)
 * 5) make "job-manager.submit" request announcing new jobid
 *
 * For performance, the above actions are batched, so that if job requests
 * arrive within the 'batch_timeout' window, they are combined into one
 * KVS transaction and one job-manager request.
 *
 * The jobid is returned to the user in response to the job-ingest.submit RPC.
 * Responses are sent after the job has been successfully ingested.
 *
 * Currently all KVS data is committed under job.<fluid-dothex>,
 * where <fluid-dothex> is the jobid converted to 16-bit, 0-padded hex
 * strings delimited by periods, e.g.
 *   job.0000.0004.b200.0000
 *
 * The job-ingest module can be loaded on rank 0, or on many ranks across
 * the instance, rank < max FLUID id - 1.  Each rank is relatively
 * independent and KVS commit scalability will ultimately limit the max
 * ingest rate for an instance.
 *
 * Security: any user with FLUX_ROLE_USER may submit jobs.  The jobspec
 * must be signed, but this module (running as the instance owner) doesn't
 * need to authenticate the signature.  It merely unwraps the contents,
 * and checks that the security envelope claims the same userid as the
 * userid stamped on the request message, which was authenticated by the
 * connector.
 */


/* The batch_timeout (seconds) is the maximum length of time
 * any given job request is delayed before initiating a KVS commit.
 * Too large, and individual job submit latency will suffer.
 * Too small, and KVS commit overhead will increase.
 */
static const double batch_timeout = 0.01;

/* There can be 2^14 FLUID generators per RFC 19.
 * Reserve the top 16 for future use.
 * This value may be set on the command line for testing.
 */
static int max_fluid_generator_id = 16384 - 16 - 1;

struct job_ingest_ctx {
    flux_t *h;
    struct pipeline *pipeline;
    uid_t owner;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec;
#else
    void *sec;
#endif
    struct fluid_generator gen;
    flux_msg_handler_t **handlers;

    struct batch *batch;
    flux_watcher_t *timer;

    int batch_count;            // if nonzero, batch by count not timer
    const char *buffer_size;

    bool shutdown;
};

struct batch {
    struct job_ingest_ctx *ctx;
    flux_kvs_txn_t *txn;
    zlist_t *jobs;
    json_t *joblist;
};

struct batch_response {
    flux_future_t *f;
    bool batch_failed;
    int errnum;
    const char *errmsg;
    zhashx_t *errors;
};

static int make_key (char *buf, int bufsz, struct job *job, const char *name);

static void batch_destroy (struct batch *batch)
{
    if (batch) {
        int saved_errno = errno;
        if (batch->jobs) {
            struct job *job;
            while ((job = zlist_pop (batch->jobs)))
                job_destroy (job);
            zlist_destroy (&batch->jobs);
            json_decref (batch->joblist);
            flux_kvs_txn_destroy (batch->txn);
        }
        free (batch);
        errno = saved_errno;
    }
}

/* Create a 'struct batch', a container for a group of job submit
 * requests.  Prepare a KVS transaction and a json array of jobid's
 * to be used for job-manager.submit request.
 */
static struct batch *batch_create (struct job_ingest_ctx *ctx)
{
    struct batch *batch;

    if (!(batch = calloc (1, sizeof (*batch))))
        return NULL;
    if (!(batch->jobs = zlist_new ()))
        goto nomem;
    if (!(batch->txn = flux_kvs_txn_create ()))
        goto error;
    if (!(batch->joblist = json_array ()))
        goto nomem;
    batch->ctx = ctx;
    return batch;
nomem:
    errno = ENOMEM;
error:
    batch_destroy (batch);
    return NULL;
}

static void batch_response_destroy (struct batch_response *bresp)
{
    if (bresp) {
        zhashx_destroy (&bresp->errors);
        flux_future_destroy (bresp->f);
        free (bresp);
    }
}

static void *jobid_duplicator (const void *item)
{
    flux_jobid_t *id = calloc (1, sizeof (flux_jobid_t));
    if (id)
        *id = *((flux_jobid_t *)item);
    return id;
}

static void jobid_destructor (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static struct batch_response *batch_response_create (flux_future_t *f)
{
    struct batch_response *bresp = NULL;
    json_t *o = NULL;
    json_t *entry = NULL;
    size_t index;

    if (!(bresp = calloc (1, sizeof (*bresp)))
        || !(bresp->errors = job_hash_create ()))
        goto error;
    zhashx_set_key_duplicator (bresp->errors, jobid_duplicator);
    zhashx_set_key_destructor (bresp->errors, jobid_destructor);
    bresp->f = f;
    flux_future_incref (f);

    /*  We differentiate future fulfilled with error (entire batch failed)
     *   and failure to unpack payload (EPROTO). This is why "future_get"
     *   is called twice below:
     */
    if (flux_rpc_get (f, NULL) < 0) {
        bresp->errnum = errno;
        bresp->errmsg = future_strerror (f, errno);
        bresp->batch_failed = true;
        return bresp;
    }
    if (flux_rpc_get_unpack (f, "{s?o}", "errors", &o) < 0) {
        errno = EPROTO;
        goto error;
    }
    /*  Empty payload indicates the whole batch was successful
     */
    if (o == NULL)
        return bresp;

    /*  O/w, there were zero or more failures sent in the errors array.
     *   Capture these in the response errors hash.
     */
    json_array_foreach (o, index, entry) {
        flux_jobid_t id;
        char *errmsg;
        if (json_unpack (entry, "[Is]", &id, &errmsg) < 0) {
            errno = EPROTO;
            goto error;
        }
        if (zhashx_insert (bresp->errors, &id, errmsg) < 0) {
            /* jobid duplicated? Should not happen */
            errno = EPROTO;
            goto error;
        }
    }
    return bresp;
error:
    batch_response_destroy (bresp);
    return NULL;
}

static void batch_respond_error (struct batch *batch,
                                 int errnum, const char *errstr)
{
    flux_t *h = batch->ctx->h;
    struct job *job = zlist_first (batch->jobs);
    while (job) {
        if (flux_respond_error (h, job->msg, errnum, errstr) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        job = zlist_next (batch->jobs);
    }
}

/* Respond to all requestors (for each job) with their id or an error if
 *  job submit failed
 */
static void batch_respond (struct batch *batch, struct batch_response *br)
{
    flux_t *h = batch->ctx->h;
    const char *errmsg;
    struct job *job = zlist_first (batch->jobs);

    if (br->batch_failed) {
        batch_respond_error (batch, br->errnum, br->errmsg);
        return;
    }

    while (job) {
        if ((errmsg = zhashx_lookup (br->errors, &job->id))) {
            if (flux_respond_error (h, job->msg, EINVAL, errmsg) < 0)
                flux_log_error (h, "batch_respond: flux_respond_error");
        }
        else if (flux_respond_pack (h, job->msg, "{s:I}", "id", job->id) < 0)
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        job = zlist_next (batch->jobs);
    }
}

static void batch_cleanup_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (h, "%s: KVS commit failed", __FUNCTION__);
    flux_future_destroy (f);
}

/* Remove KVS job entries previously committed for all failed jobs in batch.
 */
static int batch_cleanup (struct batch *batch, struct batch_response *br)
{
    flux_t *h = batch->ctx->h;
    flux_kvs_txn_t *txn;
    struct job *job;
    flux_future_t *f = NULL;
    char key[64];
    int count = 0;

    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    job = zlist_first (batch->jobs);
    while (job) {
        if (br == NULL
            || br->batch_failed
            || zhashx_lookup (br->errors, &job->id)) {
            if (make_key (key, sizeof (key), job, NULL) < 0)
                goto error;
            if (flux_kvs_txn_unlink (txn, 0, key) < 0)
                goto error;
            count++;
        }
        job = zlist_next (batch->jobs);
    }
    if (count > 0) {
        if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
            goto error;
        if (flux_future_then (f, -1., batch_cleanup_continuation, NULL) < 0)
            goto error;
    }
    flux_kvs_txn_destroy (txn);
    return 0;
error:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return -1;
}

/* Get result of announcing job(s) to job manager,
 * and respond to submit request(s).
 */
static void batch_announce_continuation (flux_future_t *f, void *arg)
{
    struct batch *batch = arg;
    struct batch_response *bresp;
    flux_t *h = batch->ctx->h;

    if (!(bresp = batch_response_create (f)))
        batch_respond_error (batch,
                             errno,
                             "Failed to process batch response");
    else
        batch_respond (batch, bresp);

    /*  Clean up any state in KVS for failed jobs
     */
    if (batch_cleanup (batch, bresp) < 0)
        flux_log_error (h, "%s: KVS cleanup failure", __FUNCTION__);

    batch_destroy (batch);
    batch_response_destroy (bresp);
    flux_future_destroy (f);
}

/* Announce job(s) to job manager.
 */
static void batch_announce (struct batch *batch)
{
    flux_t *h = batch->ctx->h;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h, "job-manager.submit", FLUX_NODEID_ANY, 0,
                             "{s:O}",
                             "jobs", batch->joblist)))
        goto error;
    if (flux_future_then (f, -1., batch_announce_continuation, batch) < 0)
        goto error;
    return;
error:
    flux_log_error (h, "%s: error sending RPC", __FUNCTION__);
    batch_respond_error (batch, errno, "error sending job-manager.submit RPC");
    if (batch_cleanup (batch, NULL) < 0)
        flux_log_error (h, "%s: KVS cleanup failure", __FUNCTION__);
    batch_destroy (batch);
    flux_future_destroy (f);
}

/* Get result of KVS commit.
 * If successful, announce job(s) to job-manager.
 */
static void batch_flush_continuation (flux_future_t *f, void *arg)
{
    struct batch *batch = arg;

    if (flux_future_get (f, NULL) < 0) {
        batch_respond_error (batch, errno, "KVS commit failed");
        batch_destroy (batch);
    }
    else {
        batch_announce (batch);
    }
    flux_future_destroy (f);
}

/*
 * Replace ctx->batch with a NULL, and pass 'batch' off to a chain of
 * continuations that commit its data to the KVS, respond to requestors,
 * and announce the new jobids.
 */
static void batch_flush (struct job_ingest_ctx *ctx)
{
    struct batch *batch;
    flux_future_t *f;

    batch = ctx->batch;
    ctx->batch = NULL;

    if (!(f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn))) {
        batch_respond_error (batch, errno, "flux_kvs_commit failed");
        goto error;
    }
    if (flux_future_then (f, -1., batch_flush_continuation, batch) < 0) {
        batch_respond_error (batch, errno, "flux_future_then (kvs) failed");
        flux_future_destroy (f);
        if (batch_cleanup (batch, NULL) < 0)
            flux_log_error (ctx->h, "%s: KVS cleanup failure", __FUNCTION__);
        goto error;
    }
    return;
error:
    batch_destroy (batch);
}

/* batch timer - expires 'batch_timeout' seconds after batch was created.
 */
static void batch_timer_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    batch_flush ((struct job_ingest_ctx *) arg);
}

/* Format key within the KVS directory of 'job'.
 */
static int make_key (char *buf, int bufsz, struct job *job, const char *name)
{
    if (flux_job_kvs_key (buf, bufsz, job->id, name) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static double get_timestamp_now (void)
{
    struct timespec ts;
    double now;

    (void)clock_gettime (CLOCK_REALTIME, &ts);
    now = ts.tv_sec;
    now += (1E-9 * ts.tv_nsec);
    return now;
}

/* Add 'job' to 'batch'.
 * On error, ensure that no remnants of job made into KVS transaction.
 */
static int batch_add_job (struct batch *batch, struct job *job)
{
    char key[64];
    int saved_errno;
    json_t *jobentry;

    if (zlist_append (batch->jobs, job) < 0) {
        errno = ENOMEM;
        return -1;
    }
    if (make_key (key, sizeof (key), job, "J") < 0)
        goto error;
    if (flux_kvs_txn_put (batch->txn, 0, key, job->J) < 0)
        goto error;
    if (make_key (key, sizeof (key), job, "jobspec") < 0)
        goto error;
    /* Drop environment from the jobspec to reduce its bulk.
     * If needed, it can be extracted from J.
     * See also flux-framework/flux-core#4520
     */
    jpath_del (job->jobspec, "attributes.system.environment");
    if (flux_kvs_txn_pack (batch->txn, 0, key, "O", job->jobspec) < 0)
        goto error;
    if (!(jobentry = json_pack ("{s:I s:I s:i s:f s:i, s:O}",
                                "id", job->id,
                                "userid", (json_int_t) job->cred.userid,
                                "urgency", job->urgency,
                                "t_submit", get_timestamp_now (),
                                "flags", job->flags,
                                "jobspec", job->jobspec)))
        goto nomem;
    if (json_array_append_new (batch->joblist, jobentry) < 0) {
        json_decref (jobentry);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    zlist_remove (batch->jobs, job);
    if (make_key (key, sizeof (key), job, NULL) == 0)
        (void)flux_kvs_txn_unlink (batch->txn, 0, key);
    errno = saved_errno;
    return -1;
}

static int ingest_add_job (struct job_ingest_ctx *ctx, struct job *job)
{
    if (fluid_generate (&ctx->gen, &job->id) < 0)
        return -1;

    /* Add job to the current "batch" of new jobs, creating the batch if
     * one doesn't exist already.  Submit is finalized upon timer expiration.
     */
    if (!ctx->batch) {
        if (!(ctx->batch = batch_create (ctx)))
            return -1;
        if (!ctx->batch_count) {
            flux_timer_watcher_reset (ctx->timer, batch_timeout, 0.);
            flux_watcher_start (ctx->timer);
        }
    }
    if (batch_add_job (ctx->batch, job) < 0)
        return -1;

    if (ctx->batch_count
        && zlist_size (ctx->batch->jobs) == ctx->batch_count)
        batch_flush (ctx);
    return 0;
}

void pipeline_continuation (flux_future_t *f, void *arg)
{
    struct job *job = arg;
    struct job_ingest_ctx *ctx = flux_future_aux_get (f, "ctx");
    flux_t *h = flux_future_get_flux (f);
    const char *errmsg = NULL;

    /* If jobspec validation failed, respond immediately to the user.
     */
    if (flux_future_get (f, NULL) < 0) {
        errmsg = future_strerror (f, errno);
        goto error;
    }

    if (ingest_add_job (ctx, job) < 0)
        goto error;

    flux_future_destroy (f);
    return;
error:
    if (flux_respond_error (h, job->msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    job_destroy (job);
    flux_future_destroy (f);
}

/* Handle "job-ingest.submit" request to add a new job.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_ingest_ctx *ctx = arg;
    struct job *job = NULL;
    const char *errmsg = NULL;
    flux_error_t error;
    flux_future_t *f = NULL;

    if (ctx->shutdown) {
        errno = ENOSYS;
        goto error;
    }
    if (!(job = job_create_from_request (msg, ctx->sec, &error))) {
        errmsg = error.text;
        goto error;
    }
    /* Do not allow root user to submit jobs in a multi-user instance.
     * The jobs will fail at runtime anyway.
     */
    if (ctx->owner != 0 && job->cred.userid == 0) {
        errmsg = "submission of jobs as user root not supported";
        goto error;
    }
    if (pipeline_process_job (ctx->pipeline, job, &f, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (f) {
        if (flux_future_then (f, -1., pipeline_continuation, job) < 0
            || flux_future_aux_set (f, "ctx", ctx, NULL) < 0) {
            goto error;
        }
    }
    else {
        if (ingest_add_job (ctx, job) < 0)
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    job_destroy (job);
    flux_future_destroy (f);
}

/* Override built-in shutdown handler that calls flux_reactor_stop().
 * Since libsubprocess clients must run in reactive mode,
 * take care of cleaning up the pipeline before exiting reactor.
 */
static void shutdown_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct job_ingest_ctx *ctx = arg;

    ctx->shutdown = true; // fail any new submit requests

    pipeline_shutdown (ctx->pipeline);
}

static void getinfo_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct job_ingest_ctx *ctx = arg;
    uint64_t timestamp;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (fluid_save_timestamp (&ctx->gen, &timestamp) < 0) {
        errno = EOVERFLOW; // punt: which is more likely, clock_gettime()
                           //       failure or flux running for 35 years?
        goto error;
    }
    if (flux_respond_pack (h, msg, "{s:I}", "timestamp", timestamp) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*  Configure job ingest from flux_conf_t and/or  argc, argv.
 *
 *  Supported configuration:
 *
 *  [ingest]
 *  batch-count = N
 *  buffer-size = "40M"
 *
 *  [ingest.validator]
 *  disable = false
 *  plugins = [ "jobspec" ]
 *  args = []
 *
 */
static int job_ingest_configure (struct job_ingest_ctx *ctx,
                                 const flux_conf_t *conf,
                                 int argc,
                                 char **argv,
                                 flux_error_t *error)
{
    flux_error_t conf_error;
    const char *buffer_size = NULL;
    const char *max_fluid_id = NULL;

    if (policy_validate (conf, error) < 0)
        return -1;
    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?{s?i s?s}}",
                          "ingest",
                            "batch-count", &ctx->batch_count,
                            "buffer-size", &buffer_size) < 0) {
        errprintf (error,
                  "error reading [ingest] config table: %s",
                  conf_error.text);
        return -1;
    }
    for (int i = 0; i < argc; i++) {
        if (strstarts (argv[i], "validator-args=")
            || strstarts (argv[i], "validator-plugins=")
            || streq (argv[i], "disable-validator")) {
            /* handled in pipeline.c */
        }
        else if (strstarts (argv[i], "batch-count=")) {
            char *endptr;
            errno = 0;
            ctx->batch_count = strtol (argv[i]+12, &endptr, 0);
            if (errno != 0 || *endptr != '\0' || ctx->batch_count < 0) {
                errprintf (error, "Invalid batch-count: %s", argv[i]);
                errno = EINVAL;
                return -1;
            }
        }
        else if (strstarts (argv[i], "buffer-size=")) {
            buffer_size = argv[i]+12;
        }
        else if (strstarts (argv[i], "max-fluid-generator-id=")) {
            max_fluid_id = argv[i] + 23;
        }
        else {
            errprintf (error, "Invalid option: %s", argv[i]);
            errno = EINVAL;
            return -1;
        }
    }
    if (buffer_size) {
        uint64_t val;
        if (parse_size (buffer_size, &val) < 0
            || val > INT_MAX)
            return errprintf (error, "Invalid buffer-size: '%s'", buffer_size);
        ctx->buffer_size = buffer_size;
        flux_log (ctx->h,
                  LOG_DEBUG,
                  "worker input buffer set to %s",
                  ctx->buffer_size);
    }
    if (max_fluid_id) {
        uint64_t val;
        if (parse_size (max_fluid_id, &val) < 0 || val > 16384 - 1)
            return errprintf (error,
                              "Invalid max-fluid-generator-id: '%s'",
                              max_fluid_id);
        max_fluid_generator_id = val;
    }
    return pipeline_configure (ctx->pipeline,
                               conf,
                               argc,
                               argv,
                               ctx->buffer_size,
                               error);
}

static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct job_ingest_ctx *ctx = arg;
    const flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0) {
        errstr = "Failed to parse config-reload request";
        goto error;
    }
    if (job_ingest_configure (ctx, conf, 0, NULL, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static void stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_ingest_ctx *ctx = arg;
    json_t *pstats = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    pstats = pipeline_stats_get (ctx->pipeline);
    if (flux_respond_pack (h,
                           msg,
                           "{s:O}",
                           "pipeline", pstats) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (pstats);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
}


static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.getinfo", getinfo_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.submit", submit_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.shutdown", shutdown_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.config-reload", reload_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.stats-get",
      stats_get_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};

int job_ingest_ctx_init (struct job_ingest_ctx *ctx,
                         flux_t *h,
                         int argc,
                         char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    memset (ctx, 0, sizeof (*ctx));
    ctx->h = h;
    flux_error_t error;

    ctx->owner = getuid ();

    /*  Default worker input buffer size is 10MB */
    ctx->buffer_size = "10M";

    if (!(ctx->pipeline = pipeline_create (h))) {
        flux_log_error (h, "error initializing job preprocessing pipeline");
        return -1;
    }
    if (job_ingest_configure (ctx, flux_get_conf (h), argc, argv, &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        return -1;
    }
#if HAVE_FLUX_SECURITY
    if (!(ctx->sec = flux_security_create (0))) {
        flux_log_error (h, "flux_security_create");
        return -1;
    }
    if (flux_security_configure (ctx->sec, NULL) < 0) {
        flux_log_error (h, "flux_security_configure: %s",
                        flux_security_last_error (ctx->sec));
        return -1;
    }
#endif
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        return -1;
    }
    if (!(ctx->timer = flux_timer_watcher_create (r, 0., 0.,
                                                  batch_timer_cb,
                                                  ctx))) {
        flux_log_error (h, "flux_timer_watcher_create");
        return -1;
    }
    return 0;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_ingest_ctx ctx;
    uint32_t rank;

    if (job_ingest_ctx_init (&ctx, h, argc, argv) < 0) {
        flux_log (h, LOG_ERR, "Failed to initialize job-ingest ctx");
        goto done;
    }

    if (flux_get_rank (h, &rank) < 0) {
        flux_log_error (h, "flux_get_rank");
        goto done;
    }
    /* If the rank exceeds the maximum FLUID generator ID, then return success
     * and let job ingest be handled upstream.
     */
    if (rank > max_fluid_generator_id) {
        flux_log (h,
                  LOG_DEBUG,
                  "job-ingest cannot allocate job IDs on ranks > %d."
                  " Exiting - upstream will handle ingest requests.",
                  max_fluid_generator_id);
        rc = 0;
        goto done;
    }
    /* Initialize FLUID generator.
     * On rank 0, derive the starting timestamp from the job manager's
     * 'max_jobid' plus one.  On other ranks, ask upstream job-ingest.
     */
    if (rank == 0) {
        flux_future_t *f;
        flux_jobid_t max_jobid;

        if (!(f = flux_rpc (h, "job-manager.getinfo", NULL, 0, 0))) {
            flux_log_error (h, "flux_rpc");
            goto done;
        }
        if (flux_rpc_get_unpack (f, "{s:I}", "max_jobid", &max_jobid) < 0) {
            if (errno == ENOSYS)
                flux_log_error (h, "job-manager must be loaded first");
            else
                flux_log_error (h, "job-manager.getinfo");
            flux_future_destroy (f);
            goto done;
        }
        flux_future_destroy (f);
        if (fluid_init (&ctx.gen, 0, fluid_get_timestamp (max_jobid) + 1) < 0) {
            flux_log (h, LOG_ERR, "fluid_init failed");
            errno = EINVAL;
            goto done;
        }
    }
    else {
        flux_future_t *f;
        uint64_t timestamp;

        if (!(f = flux_rpc (h, "job-ingest.getinfo", NULL, 0, 0))) {
            flux_log_error (h, "flux_rpc");
            goto done;
        }
        if (flux_rpc_get_unpack (f, "{s:I}", "timestamp", &timestamp) < 0) {
            if (errno == ENOSYS)
                flux_log_error (h, "job-ingest must be loaded on rank 0 first");
            else
                flux_log_error (h, "job-ingest.getinfo");
            flux_future_destroy (f);
            goto done;
        }
        flux_future_destroy (f);
        if (fluid_init (&ctx.gen, rank, timestamp) < 0) {
            flux_log (h, LOG_ERR, "fluid_init failed");
            errno = EINVAL;
            goto done;
        }
    }
    flux_log (h, LOG_DEBUG, "fluid ts=%jums", (uint64_t)ctx.gen.timestamp);
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    flux_watcher_destroy (ctx.timer);
#if HAVE_FLUX_SECURITY
    flux_security_destroy (ctx.sec);
#endif
    pipeline_destroy (ctx.pipeline);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
