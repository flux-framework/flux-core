/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* purge.c - remove old inactive jobs
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libccan/ccan/ptrint/ptrint.h"

#include "job-manager.h"
#include "job.h"
#include "purge.h"
#include "conf.h"
#include "jobtap-internal.h"
#include "restart.h"

#define INACTIVE_NUM_UNLIMITED  (-1)
#define INACTIVE_AGE_UNLIMITED  (-1.)

struct purge {
    struct job_manager *ctx;
    double age_limit;
    int num_limit;
    zlistx_t *queue;
    flux_future_t *f_sync;
    flux_future_t *f_purge;

    flux_msg_handler_t **handlers;
    struct flux_msglist *requests;
};

static const int purge_batch_max = 100; // max KVS ops per txn

/* Add an inactive job to the "purge queue".
 * The queue is ordered by the time the job became inactive, so
 * the first job in the queue is the oldest.
 */
int purge_enqueue_job (struct purge *purge, struct job *job)
{
    assert (job->handle == NULL);
    if (!(job->handle = zlistx_insert (purge->queue, job, false))) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int purge_publish (struct purge *purge, json_t *jobs)
{
    flux_future_t *f;

    if (!(f = flux_event_publish_pack (purge->ctx->h,
                                       "job-purge-inactive",
                                       0,
                                       "{s:O}",
                                       "jobs", jobs)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

/* Return true if candidate job is eligible for purging based on
 * provided limits.  A job is purgeable if either limit is exceeded.
 */
static bool purge_eligible (double age,
                            double age_limit,
                            int num,
                            int num_limit)
{
    if (age_limit != INACTIVE_AGE_UNLIMITED && age > age_limit)
        return true;
    if (num_limit != INACTIVE_NUM_UNLIMITED && num > num_limit)
        return true;
    return false;
}

static int purge_eligible_count (struct purge *purge,
                                 double age_limit,
                                 int num_limit)
{
    double now = flux_reactor_now (flux_get_reactor (purge->ctx->h));
    struct job *job;
    int count = 0;

    job = zlistx_first (purge->queue);
    while (job) {
        if (!purge_eligible (now - job->t_clean,
                             age_limit,
                             zlistx_size (purge->queue) - count,
                             num_limit))
            break;
        count++;
        job = zlistx_next (purge->queue);
    }
    return count;
}

/* Send a KVS commit containing unlinks for one or more inactive jobs.
 * Return future if successful, with 'count' added to aux hash.
 * Return NULL on failure with errno set.
 * N.B. Failure with errno=ENODATA just means there were no eligible jobs.
 */
static flux_future_t *purge_inactive_jobs (struct purge *purge,
                                           double age_limit,
                                           int num_limit,
                                           int max_purge_count)
{
    double now = flux_reactor_now (flux_get_reactor (purge->ctx->h));
    struct job *job;
    flux_kvs_txn_t *txn;
    json_t *jobs = NULL;
    char key[64];
    flux_future_t *f = NULL;
    int count = 0;

    if (!(txn = flux_kvs_txn_create ()))
        return NULL;
    if (!(jobs = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    while ((job = zlistx_first (purge->queue)) && count < max_purge_count) {
        if (!purge_eligible (now - job->t_clean,
                             age_limit,
                             zlistx_size (purge->queue),
                             num_limit))
            break;
        if (flux_job_kvs_key (key, sizeof (key), job->id, NULL) < 0
            || flux_kvs_txn_unlink (txn, 0, key) < 0)
            goto error;

        /* Update max_jobid kvs entry if we are purging it.
         * See also flux-framework/flux-core#4300.
         */
        if (job->id == purge->ctx->max_jobid) {
            if (restart_save_state_to_txn (purge->ctx, txn) < 0)
                flux_log_error (purge->ctx->h,
                    "Error adding job-manager state to purge transaction");
        }

        json_t *o = json_integer (job->id);
        if (!o || json_array_append_new (jobs, o)) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }

        (void)jobtap_call (purge->ctx->jobtap,
                           job,
                           "job.inactive-remove",
                           NULL);

        (void)zlistx_delete (purge->queue, job->handle);
        job->handle = NULL;
        zhashx_delete (purge->ctx->inactive_jobs, &job->id);
        count++;
    }
    if (count == 0) {
        errno = ENODATA;
        goto error;
    }
    if (!(f = flux_kvs_commit (purge->ctx->h, NULL, 0, txn))
        || flux_future_aux_set (f, "count", int2ptr (count), NULL) < 0
        || purge_publish (purge, jobs) < 0)
        goto error;
    flux_kvs_txn_destroy (txn);
    json_decref (jobs);
    /* N.B. if kvs commit fails, jobs are still removed from the hash/list.
     * It doesn't seem worth the effort to structure the code to avoid this
     * due to high complexity of solution, low probability of error, and
     * minor consequences.
     */
    return f;
error:
    flux_kvs_txn_destroy (txn);
    json_decref (jobs);
    flux_future_destroy (f);
    return NULL;
}

/* Complete periodic purge.
 */
static void purge_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct purge *purge = arg;
    int count = ptr2int (flux_future_aux_get (f, "count"));

    if (flux_rpc_get (f, NULL) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "error committing purge KVS transaction: %s",
                  future_strerror (f, errno));
        goto done;
    }
    flux_log (h, LOG_DEBUG, "purged %d inactive jobs", count);
done:
    if (purge->f_purge == f)
        purge->f_purge = NULL;
    flux_future_destroy (f);
}

/* Periodically check for inactive jobs that meet purge criteria, if
 * criteria are configured.  If not configured, this callback is not enabled.
 */
static void sync_cb (flux_future_t *f_sync, void *arg)
{
    flux_t *h = flux_future_get_flux (f_sync);
    struct purge *purge = arg;

    if (flux_future_get (f_sync, NULL) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "purge synchronization error: %s",
                  future_strerror (f_sync, errno));
    }
    if (!(purge->f_purge)) {
        flux_future_t *f;
        if (!(f = purge_inactive_jobs (purge,
                                       purge->age_limit,
                                       purge->num_limit,
                                       purge_batch_max))
            || flux_future_then (f, -1, purge_continuation, purge) < 0) {
            flux_future_destroy (f);
            if (errno != ENODATA)
                flux_log_error (h, "error creating purge KVS transaction");
            goto done;
        }
        purge->f_purge = f;
    }
done:
    flux_future_reset (f_sync);
}

/* Start or stop heartbeat driven sync callback after configuration change.
 */
int purge_sync_update (struct purge *purge)
{
    if (purge->age_limit != INACTIVE_AGE_UNLIMITED
        || purge->num_limit != INACTIVE_NUM_UNLIMITED) {
        if (!purge->f_sync) {
            if (!(purge->f_sync = flux_sync_create (purge->ctx->h, 0.))
                || flux_future_then (purge->f_sync, -1, sync_cb, purge) < 0) {
                flux_future_destroy (purge->f_sync);
                purge->f_sync = NULL;
                return -1;
            }
        }
    }
    else {
        if (purge->f_sync) {
            flux_future_destroy (purge->f_sync);
            purge->f_sync = NULL;
        }
    }
    return 0;
}

/* Locate the message containing 'f'.
 * Side effect: cursor is parked on this message, so flux_msglist_delete()
 * can be used to delete it later.
 */
static const flux_msg_t *find_request (struct flux_msglist *l, flux_future_t *f)
{
    const flux_msg_t *msg;

    msg = flux_msglist_first (l);
    while (msg) {
        if (flux_msg_aux_get (msg, "future") == f)
            return msg;
        msg = flux_msglist_next (l);
    }
    return NULL;
}

static void purge_request_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct purge *purge = arg;
    int count = ptr2int (flux_future_aux_get (f, "count"));
    const flux_msg_t *msg = find_request (purge->requests, f);

    assert (msg != NULL);
    if (flux_rpc_get (f, NULL) < 0) {
        if (flux_respond_error (h, msg, errno, future_strerror (f, errno)) < 0)
            flux_log_error (h, "error responding to purge request");
        goto done;
    }
    if (flux_respond_pack (h, msg, "{s:i}", "count", count) < 0)
        flux_log_error (h, "error responding to purge request");
done:
    // assumes cursor still positioned from find_request(), and destroys f
    flux_msglist_delete (purge->requests);
}

static void purge_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct purge *purge = arg;
    double age_limit;
    int num_limit;
    int batch;
    int force;
    flux_future_t *f;
    const char *errmsg = NULL;
    flux_error_t error;
    int count = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:f s:i s:i s:b}",
                             "age_limit", &age_limit,
                             "num_limit", &num_limit,
                             "batch", &batch,
                             "force", &force) < 0)
        goto error;
    if (age_limit != INACTIVE_AGE_UNLIMITED && age_limit < 0) {
        errmsg = "if set, age limit must be >= 0";
        errno = EINVAL;
        goto error;
    }
    if (num_limit != INACTIVE_NUM_UNLIMITED && num_limit < 0) {
        errmsg = "if set, num limit must be >= 0";
        errno = EINVAL;
        goto error;
    }
    if (batch < 1 || batch > purge_batch_max) {
        errprintf (&error, "batch must be >= 1 and <= %d", purge_batch_max);
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (!force) { // just return count
        count = purge_eligible_count (purge, age_limit, num_limit);
        goto done;
    }
    if (!(f = purge_inactive_jobs (purge,
                                   age_limit,
                                   num_limit,
                                   batch))
        || flux_future_then (f, -1, purge_request_continuation, purge) < 0) {
        flux_future_destroy (f);
        if (errno == ENODATA) // ENODATA means zero jobs can be purged
            goto done;
        goto error;
    }
    if (flux_msg_aux_set (msg,
                          "future",
                          f,
                          (flux_free_f)flux_future_destroy) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    if (flux_msglist_append (purge->requests, msg) < 0)
        goto error; // future destroyed with msg by dispatcher
    return;
done:
    if (flux_respond_pack (h, msg, "{s:i}", "count", count) < 0)
        flux_log_error (h, "error responding to purge request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to purge request");
}

static int purge_parse_config (const flux_conf_t *conf,
                               flux_error_t *error,
                               void *arg)
{
    struct purge *purge = arg;
    flux_error_t e;
    const char *fsd = NULL;
    double age_limit = INACTIVE_AGE_UNLIMITED;
    int num_limit = INACTIVE_NUM_UNLIMITED;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?{s?s s?i}}",
                          "job-manager",
                            "inactive-age-limit", &fsd,
                            "inactive-num-limit", &num_limit) < 0)
        return errprintf (error, "job-manager.max-inactive-*: %s", e.text);
    if (fsd) {
        double t;
        if (fsd_parse_duration (fsd, &t) < 0)
            return errprintf (error,
                              "job-manager.inactive-age-limit: invalid FSD");
        age_limit = t;
    }
    if (num_limit != INACTIVE_NUM_UNLIMITED) {
        if (num_limit < 0)
            return errprintf (error,
                              "job-manager.inactive-num-limit: must be >= 0");
    }
    purge->age_limit = age_limit;
    purge->num_limit = num_limit;

    if (purge_sync_update (purge) < 0)
        flux_log_error (purge->ctx->h, "could not start purge sync callbacks");
    return 1; // indicates to conf.c that callback wants updates
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.purge", purge_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void purge_destroy (struct purge *purge)
{
    if (purge) {
        int saved_errno = errno;
        flux_msg_handler_delvec (purge->handlers);
        zlistx_destroy (&purge->queue);
        flux_msglist_destroy (purge->requests);
        conf_unregister_callback (purge->ctx->conf, purge_parse_config);
        flux_future_destroy (purge->f_sync);
        flux_future_destroy (purge->f_purge);
        free (purge);
        errno = saved_errno;
    }
}

struct purge *purge_create (struct job_manager *ctx)
{
    struct purge *purge;
    flux_error_t error;

    if (!(purge = calloc (1, sizeof (*purge))))
        return NULL;
    purge->ctx = ctx;
    purge->age_limit = INACTIVE_AGE_UNLIMITED;
    purge->num_limit = INACTIVE_NUM_UNLIMITED;

    if (!(purge->queue = zlistx_new()))
        goto error;
    zlistx_set_destructor (purge->queue, job_destructor);
    zlistx_set_comparator (purge->queue, job_age_comparator);
    zlistx_set_duplicator (purge->queue, job_duplicator);

    if (conf_register_callback (ctx->conf,
                                &error,
                                purge_parse_config,
                                purge) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "error parsing job-manager config: %s",
                  error.text);
        goto error;
    }
    if (flux_msg_handler_addvec (ctx->h, htab, purge, &purge->handlers) < 0)
        goto error;
    if (!(purge->requests = flux_msglist_create ()))
        goto error;
    return purge;
error:
    purge_destroy (purge);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
