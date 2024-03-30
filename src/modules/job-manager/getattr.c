/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* getattr - fetch job information about one job
 *
 * Purpose:
 *   Expose job manager internals for testing.
 *
 * Input:
 * - List of attributes
 *
 * Output:
 * - Dictionary of attributes and values.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "job.h"
#include "job-manager.h"

#include "getattr.h"

static json_t *make_dict (struct job *job,
                          json_t *attrs,
                          flux_error_t *errp)
{
    size_t index;
    json_t *val;
    json_t *dict;

    if (!(dict = json_object ()))
        goto nomem;
    json_array_foreach (attrs, index, val) {
        const char *key = json_string_value (val);
        if (!key) {
            errprintf (errp, "attribute list contains non-string");
            errno = EPROTO;
            goto error;
        }
        if (streq (key, "jobspec")) {
            if (!job->jobspec_redacted) {
                errprintf (errp, "jobspec is NULL");
                errno = ENOENT;
                goto error;
            }
            if (json_object_set (dict, key, job->jobspec_redacted) < 0)
                goto nomem;
        }
        else if (streq (key, "R")) {
            if (!job->R_redacted) {
                errprintf (errp, "R is NULL");
                errno = ENOENT;
                goto error;
            }
            if (json_object_set (dict, key, job->R_redacted) < 0)
                goto nomem;
        }
        else if (streq (key, "eventlog")) {
            if (json_object_set (dict, key, job->eventlog) < 0)
                goto nomem;
        }
        else {
            errprintf (errp, "unknown attr %s", key);
            errno = ENOENT;
            goto error;
        }
    }
    return dict;
nomem:
    errprintf (errp, "out of memory");
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, dict);
    return NULL;
}

void getattr_handle_request (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    struct job *job;
    json_t *attrs;
    const char *errstr = NULL;
    flux_error_t error;
    json_t *dict = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:o}",
                             "id", &id,
                             "attrs", &attrs) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))
        && !(job = zhashx_lookup (ctx->inactive_jobs, &id))) {
        errstr = "unknown job";
        errno = EINVAL;
        goto error;
    }
    /* Security: guests can only access their own jobs
     */
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only reprioritize their own jobs";
        goto error;
    }
    if (!(dict = make_dict (job, attrs, &error))) {
        errstr = error.text;
        goto error;
    }
    if (flux_respond_pack (h, msg, "O", dict) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (dict);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (dict);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
