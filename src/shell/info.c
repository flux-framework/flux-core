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
#include "src/common/libresource/rset.h"

#include "info.h"

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
                                      uint32_t broker_rank,
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
    info->broker_rank = broker_rank;
    info->verbose = verbose;
    if (!R || !jobspec) {
        if (!(f = lookup_job_info (h, jobid, jobspec, R)))
            goto error;
        if (lookup_job_info_get (f, &jobspec, &R) < 0)
            goto error;
    }
    if (!(info->jobspec = json_loads (jobspec, 0, &error))) {
        log_msg ("error decoding jobspec: %s", error.text);
        goto error;
    }
    if (!(info->rset = resource_set_create (R, &error))) {
        log_msg ("error decoding R: %s", error.text);
        goto error;
    }
    if (!(info->rlocal = resource_set_children (info->rset, broker_rank))) {
        log_msg ("R_local is empty");
        goto error;
    }
    if (verbose) {
        char *s = json_dumps (info->rlocal, JSON_COMPACT);
        log_msg ("R_local: %s", s ? s : "NULL");
        free (s);
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
        resource_set_destroy (info->rset); // invalidates info->rlocal also
        json_decref (info->jobspec);
        free (info);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
