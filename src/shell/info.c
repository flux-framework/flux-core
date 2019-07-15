/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/log.h"

#include "info.h"
#include "jobspec.h"

/* Append string 's' to JSON array 'array'.
 * Return 0 on success, -1 on failure.
 */
static int array_append_string (json_t *array, const char *s)
{
    json_t *o;

    if (!(o = json_string (s)) || json_array_append_new (array, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

/* If either *jobspec or *R is NULL, fetch it from future and assign.
 * Return 0 on success, -1 on failure (and log error).
 * N.B. assigned values remain valid until future is destroyed.
 */
static int lookup_job_info_get (flux_future_t *f,
                                const char **jobspec,
                                const char **R)
{
    if (!*jobspec && flux_rpc_get_unpack (f, "{s:s}", "jobspec", jobspec) < 0)
        goto error;
    if (!*R && flux_rpc_get_unpack (f, "{s:s}", "R", R) < 0)
        goto error;
    return 0;
error:
    log_msg ("job-info: %s", future_strerror (f, errno));
    return -1;
}

/* If either jobspec or R is NULL, fetch it from the job-info service.
 * Return future on success or NULL on failure (and log error).
 */
static flux_future_t *lookup_job_info (flux_t *h,
                                       flux_jobid_t jobid,
                                       const char *jobspec,
                                       const char *R)
{
    json_t *keys;
    flux_future_t *f;

    if (!(keys = json_array ())
            || (!R && array_append_string (keys, "R") < 0)
            || (!jobspec && array_append_string (keys, "jobspec") < 0)) {
        log_msg ("error building json array");
        return NULL;
    }
    f = flux_rpc_pack (h,
                       "job-info.lookup",
                       FLUX_NODEID_ANY,
                       0,
                       "{s:I s:O s:i}",
                       "id", jobid,
                       "keys", keys,
                       "flags", 0);
    if (!f)
        log_msg ("error sending job-info request");
    json_decref (keys);
    return f;
}

struct shell_info *shell_info_create (flux_t *h,
                                      flux_jobid_t jobid,
                                      int broker_rank,
                                      const char *jobspec,
                                      const char *R,
                                      bool verbose)
{
    struct shell_info *info;
    json_error_t error;
    flux_future_t *f = NULL;

    if (!(info = calloc (1, sizeof (*info)))) {
        log_err ("shell_info_create");
        return NULL;
    }
    info->jobid = jobid;
    info->verbose = verbose;
    if (broker_rank == -1) {
        uint32_t rank;
        if (!h) {
            log_err ("Invalid arguments: h==NULL and broker_rank is unset");
            goto error;
        }
        if (flux_get_rank (h, &rank) < 0) {
            log_err ("error fetching broker rank");
            goto error;
        }
        broker_rank = rank;
    }
    if (!R || !jobspec) {
        if (!h) {
            log_err ("Invalid arguments: h==NULL and R or jobspec are unset");
            goto error;
        }
        if (!(f = lookup_job_info (h, jobid, jobspec, R)))
            goto error;
        if (lookup_job_info_get (f, &jobspec, &R) < 0)
            goto error;
    }
    if (!(info->jobspec = jobspec_parse (jobspec, &error))) {
        log_msg ("error parsing jobspec: %s", error.text);
        goto error;
    }
    if (!(info->rcalc = rcalc_create (R))) {
        log_msg ("error decoding R");
        goto error;
    }
    if (rcalc_distribute (info->rcalc, info->jobspec->task_count) < 0) {
        log_msg ("error distributing %d tasks over R",
                 info->jobspec->task_count);
        goto error;
    }
    if (rcalc_get_rankinfo (info->rcalc, broker_rank, &info->rankinfo) < 0) {
        log_msg ("error fetching rankinfo for rank %d", broker_rank);
        goto error;
    }
    info->shell_size = rcalc_total_nodes (info->rcalc);
    info->shell_rank = info->rankinfo.nodeid;
    if (verbose) {
        if (info->shell_rank == 0)
            log_msg ("0: task_count=%d slot_count=%d "
                     "cores_per_slot=%d slots_per_node=%d",
                     info->jobspec->task_count,
                     info->jobspec->slot_count,
                     info->jobspec->cores_per_slot,
                     info->jobspec->slots_per_node);
        if (info->rankinfo.ntasks > 1)
            log_msg ("%d: tasks [%d-%d] on cores %s",
                     info->shell_rank,
                     info->rankinfo.global_basis,
                     info->rankinfo.global_basis + info->rankinfo.ntasks - 1,
                     info->rankinfo.cores);
        else
            log_msg ("%d: tasks [%d] on cores %s",
                     info->shell_rank,
                     info->rankinfo.global_basis,
                     info->rankinfo.cores);
    }
    flux_future_destroy (f);
    return info;
error:
    flux_future_destroy (f);
    shell_info_destroy (info);
    return NULL;
}

void shell_info_destroy (struct shell_info *info)
{
    if (info) {
        int saved_errno = errno;
        jobspec_destroy (info->jobspec);
        rcalc_destroy (info->rcalc);
        free (info);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
