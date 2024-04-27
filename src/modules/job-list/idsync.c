/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* idsync.c - code to sync job ids if job-list not yet aware of them */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job_hash.h"

#include "idsync.h"
#include "job_util.h"

/* Used in waits hash, need to store job id within data structure for lookup */
struct idsync_wait_list {
    zlistx_t *l;
    flux_jobid_t id;
};

void idsync_data_destroy (void *data)
{
    if (data) {
        struct idsync_data *isd = data;
        int save_errno = errno;
        flux_msg_destroy (isd->msg);
        json_decref (isd->attrs);
        flux_future_destroy (isd->f_lookup);
        free (isd);
        errno = save_errno;
    }
}

/* czmq_destructor */
static void idsync_data_destroy_wrapper (void **data)
{
    if (data) {
        idsync_data_destroy (*data);
        *data = NULL;
    }
}

static struct idsync_data *idsync_data_create (flux_t *h,
                                               flux_jobid_t id,
                                               const flux_msg_t *msg,
                                               json_t *attrs,
                                               flux_job_state_t state,
                                               flux_future_t *f_lookup)
{
    struct idsync_data *isd = NULL;

    isd = calloc (1, sizeof (*isd));
    if (!isd)
        goto error_enomem;
    isd->h = h;
    isd->id = id;
    if (!(isd->msg = flux_msg_copy (msg, false)))
        goto error;
    isd->attrs = json_incref (attrs);
    isd->state = state;
    isd->f_lookup = f_lookup;
    return isd;

 error_enomem:
    errno = ENOMEM;
 error:
    idsync_data_destroy (isd);
    return NULL;
}

static void idsync_wait_list_destroy (void **data)
{
    if (data) {
        struct idsync_wait_list *iwl = *data;
        if (iwl) {
            zlistx_destroy (&iwl->l);
            free (iwl);
        }
        *data = NULL;
    }
}

struct idsync_ctx *idsync_ctx_create (flux_t *h)
{
    struct idsync_ctx *isctx = NULL;
    int saved_errno;

    if (!(isctx = calloc (1, sizeof (*isctx))))
        return NULL;
    isctx->h = h;

    if (!(isctx->lookups = zlistx_new ()))
        goto error;

    zlistx_set_destructor (isctx->lookups, idsync_data_destroy_wrapper);

    if (!(isctx->waits = job_hash_create ()))
        goto error;

    zhashx_set_destructor (isctx->waits, idsync_wait_list_destroy);

    return isctx;

error:
    saved_errno = errno;
    idsync_ctx_destroy (isctx);
    errno = saved_errno;
    return NULL;
}

void idsync_ctx_destroy (struct idsync_ctx *isctx)
{
    if (isctx) {
        struct idsync_data *isd;
        isd = zlistx_first (isctx->lookups);
        while (isd) {
            if (isd->f_lookup) {
                if (flux_future_get (isd->f_lookup, NULL) < 0)
                    flux_log_error (isctx->h, "%s: flux_future_get",
                                    __FUNCTION__);
            }
            isd = zlistx_next (isctx->lookups);
        }
        zlistx_destroy (&isctx->lookups);
        zhashx_destroy (&isctx->waits);
        free (isctx);
    }
}

struct idsync_data *idsync_check_id_valid (struct idsync_ctx *isctx,
                                           flux_jobid_t id,
                                           const flux_msg_t *msg,
                                           json_t *attrs,
                                           flux_job_state_t state)
{
    flux_future_t *f = NULL;
    struct idsync_data *isd = NULL;
    char path[256];

    /* Check to see if the ID is legal, job-list may have not yet
     * seen the ID publication yet */
    if (flux_job_kvs_key (path, sizeof (path), id, NULL) < 0)
        goto error;

    if (!(f = flux_kvs_lookup (isctx->h, NULL, FLUX_KVS_READDIR, path))) {
        flux_log_error (isctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto error;
    }

    if (!(isd = idsync_data_create (isctx->h, id, msg, attrs, state, f)))
        goto error;

    /* future now owned by struct idsync_data */
    f = NULL;

    if (!zlistx_add_end (isctx->lookups, isd)) {
        flux_log (isctx->h, LOG_ERR, "%s: zlistx_add_end", __FUNCTION__);
        errno = ENOMEM;
        goto error;
    }

    return isd;

error:
    flux_future_destroy (f);
    idsync_data_destroy (isd);
    return NULL;
}

void idsync_check_id_valid_cleanup (struct idsync_ctx *isctx,
                                    struct idsync_data *isd)
{
    /* delete will destroy struct idsync_data and future within it */
    void *handle = zlistx_find (isctx->lookups, isd);
    if (handle)
        zlistx_delete (isctx->lookups, handle);
}

static int idsync_add_waiter (struct idsync_ctx *isctx,
                              struct idsync_data *isd)
{
    struct idsync_wait_list *iwl = NULL;

    /* isctx->waits holds lists of ids waiting on, b/c multiple callers
     * could wait on same id */
    if (!(iwl = zhashx_lookup (isctx->waits, &isd->id))) {
        iwl = calloc (1, sizeof (*iwl));
        if (!iwl)
            goto enomem;

        if (!(iwl->l = zlistx_new ()))
            goto enomem;
        zlistx_set_destructor (iwl->l, idsync_data_destroy_wrapper);
        iwl->id = isd->id;

        (void)zhashx_insert (isctx->waits, &iwl->id, iwl);
    }

    if (!zlistx_add_end (iwl->l, isd))
        goto enomem;

    return 0;

enomem:
    idsync_wait_list_destroy ((void **)&iwl);
    errno = ENOMEM;
    return -1;
}

int idsync_wait_valid (struct idsync_ctx *isctx, struct idsync_data *isd)
{
    void *handle;

    /* make sure isd isn't on lookups list, if so remove it */
    if ((handle = zlistx_find (isctx->lookups, isd))) {
        /* detach will not call zlistx destructor */
        zlistx_detach (isctx->lookups, handle);
    }

    return idsync_add_waiter (isctx, isd);
}


int idsync_wait_valid_id (struct idsync_ctx *isctx,
                          flux_jobid_t id,
                          const flux_msg_t *msg,
                          json_t *attrs,
                          flux_job_state_t state)
{
    struct idsync_data *isd = NULL;

    if (!(isd = idsync_data_create (isctx->h, id, msg, attrs, state, NULL)))
        return -1;

    return idsync_add_waiter (isctx, isd);
}

static void idsync_data_respond (struct idsync_ctx *isctx,
                                 struct idsync_data *isd,
                                 struct job *job)
{
    flux_error_t err;
    json_t *o;

    if (!(o = job_to_json (job, isd->attrs, &err)))
        goto error;

    if (flux_respond_pack (isctx->h, isd->msg, "{s:O}", "job", o) < 0)
        flux_log_error (isctx->h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (o);
    return;

error:
    if (flux_respond_error (isctx->h, isd->msg, errno, err.text) < 0)
        flux_log_error (isctx->h, "%s: flux_respond_error", __FUNCTION__);
}

void idsync_check_waiting_id (struct idsync_ctx *isctx, struct job *job)
{
    struct idsync_wait_list *iwl;

    if ((iwl = zhashx_lookup (isctx->waits, &job->id))) {
        struct idsync_data *isd;
        isd = zlistx_first (iwl->l);
        while (isd) {
            /* Some job states can be missed.  For example a job that
             * is canceled before it runs will never reach the
             * FLUX_JOB_STATE_RUN state.  To ensure jobs waiting on
             * states that are missed will eventually get a response, always
             * respond once the job has reached the inactive state.
             */
            if (!isd->state
                || (isd->state & job->states_mask)
                || (isd->state && job->state == FLUX_JOB_STATE_INACTIVE)) {
                struct idsync_data *tmp;
                idsync_data_respond (isctx, isd, job);
                tmp = zlistx_detach_cur (iwl->l);
                idsync_data_destroy (tmp);
            }
            isd = zlistx_next (iwl->l);
        }
        if (!zlistx_size (iwl->l))
            zhashx_delete (isctx->waits, &job->id);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
