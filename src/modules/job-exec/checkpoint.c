/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Prototype checkpoint of running jobs KVS root refs
 *
 * DESCRIPTION
 *
 * Handle checkpoint of running job's guest KVS namescapes into the
 * primary KVS.  This will allow the namespaces to be recreated if
 * a job manager is brought down than back up.
 *
 * OPERATION
 *
 * Get the KVS rootrefs for all running jobs and commit to
 * "job-exec.kvs-namespaces".
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job-exec.h"
#include "checkpoint.h"

static int lookup_nsroots (flux_t *h, zhashx_t *jobs, flux_future_t **fp)
{
    struct jobinfo *job = zhashx_first (jobs);
    flux_future_t *fall = NULL;
    flux_future_t *f = NULL;

    while (job) {
        if (job->running) {
            if (!fall) {
                if (!(fall = flux_future_wait_all_create ()))
                    goto cleanup;
                flux_future_set_flux (fall, h);
            }
            if (!(f = flux_kvs_getroot (h, job->ns, 0)))
                goto cleanup;
            if (flux_future_aux_set (f, "jobinfo", job, NULL) < 0)
                goto cleanup;
            if (flux_future_push (fall, job->ns, f) < 0)
                goto cleanup;
            f = NULL;
        }
        job = zhashx_next (jobs);
    }

    (*fp) = fall;
    return 0;

cleanup:
    flux_future_destroy (f);
    flux_future_destroy (fall);
    return -1;
}

static json_t *get_nsroots (flux_t *h, flux_future_t *fall)
{
    const char *child;
    json_t *nsdata = NULL;
    int saved_errno;

    if (!(nsdata = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }

    child = flux_future_first_child (fall);
    while (child) {
        flux_future_t *f = flux_future_get_child (fall, child);
        struct jobinfo *job;
        const char *blobref = NULL;
        json_t *o = NULL;
        if (!f)
            goto cleanup;
        if (!(job = flux_future_aux_get (f, "jobinfo")))
            goto cleanup;
        if (flux_kvs_getroot_get_blobref (f, &blobref) < 0)
            goto cleanup;
        if (!(o = json_pack ("{s:I s:i s:s}",
                             "id", job->id,
                             "owner", job->userid,
                             "kvsroot", blobref))) {
            errno = ENOMEM;
            goto cleanup;
        }
        if (json_array_append (nsdata, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto cleanup;
        }
        json_decref (o);
        child = flux_future_next_child (fall);
    }

    return nsdata;
cleanup:
    saved_errno = errno;
    json_decref (nsdata);
    errno = saved_errno;
    return NULL;
}

static int checkpoint_commit (flux_t *h, json_t *nsdata)
{
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn = NULL;
    char *s = NULL;
    int rv = -1;

    if (!(s = json_dumps (nsdata, JSON_COMPACT))) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (!(txn = flux_kvs_txn_create ()))
        goto cleanup;

    if (flux_kvs_txn_put (txn,
                          0,
                          "job-exec.kvs-namespaces",
                          s) < 0)
        goto cleanup;

    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        goto cleanup;

    if (flux_future_get (f, NULL) < 0)
        goto cleanup;

    rv = 0;
cleanup:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (s);
    return rv;
}

void checkpoint_running (flux_t *h, zhashx_t *jobs)
{
    flux_future_t *lookupf = NULL;
    json_t *nsdata = NULL;

    if (!h || !jobs)
        return;

    if (lookup_nsroots (h, jobs, &lookupf) < 0) {
        flux_log_error (h, "failed to lookup ns root refs");
        goto cleanup;
    }

    if (!lookupf)
        return;

    if (!(nsdata = get_nsroots (h, lookupf))) {
        flux_log_error (h, "failure getting ns root refs");
        goto cleanup;
    }

    if (checkpoint_commit (h, nsdata) < 0) {
        flux_log_error (h, "failure committing ns checkpoint data");
        goto cleanup;
    }

cleanup:
    json_decref (nsdata);
    flux_future_destroy (lookupf);
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
