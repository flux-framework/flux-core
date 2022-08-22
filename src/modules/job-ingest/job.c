/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/context.h>
#include <flux/security/sign.h>
#endif

#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/sign_none.h"

#include "job.h"

/* Free decoded jobspec after it has been transferred to the batch txn,
 * to conserve memory.
 */
void job_clean (struct job *job)
{
    if (job) {
        free (job->jobspec);
        job->jobspec = NULL;
        json_decref (job->jobspec_obj);
        job->jobspec_obj = NULL;
    }
}

void job_destroy (struct job *job)
{
    if (job) {
        int saved_errno = errno;
        free (job->jobspec);
        flux_msg_decref (job->msg);
        json_decref (job->jobspec_obj);
        free (job);
        errno = saved_errno;
    }
}

static int valid_flags (int flags)
{
    int allowed = FLUX_JOB_DEBUG | FLUX_JOB_WAITABLE | FLUX_JOB_NOVALIDATE;
    if ((flags & ~allowed)) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

struct job *job_create_from_request (const flux_msg_t *msg,
                                     void *security_context,
                                     flux_error_t *error)
{
    struct job *job;
    int64_t userid_signer;
    const char *mech_type;
    json_error_t json_error;

    if (!(job = calloc (1, sizeof (*job)))) {
        errprintf (error, "out of memory decoding job request");
        return NULL;
    }
    job->msg = flux_msg_incref (msg);
    if (flux_request_unpack (job->msg,
                             NULL,
                             "{s:s s:i s:i}",
                             "J", &job->J,
                             "urgency", &job->urgency,
                             "flags", &job->flags) < 0
        || flux_msg_get_cred (job->msg, &job->cred) < 0) {
        errprintf (error, "error decoding job request: %s", strerror (errno));
        goto error;
    }
    if (valid_flags (job->flags) < 0) {
        errprintf (error, "invalid job flags");
        goto error;
    }
    if (!(job->cred.rolemask & FLUX_ROLE_OWNER)
        && (job->flags & FLUX_JOB_NOVALIDATE)) {
        errprintf (error,
                   "only the instance owner can submit "
                   "with FLUX_JOB_NOVALIDATE");
        errno = EPERM;
        goto error;
    }
    if (job->urgency < FLUX_JOB_URGENCY_MIN
        || job->urgency > FLUX_JOB_URGENCY_MAX) {
        errprintf (error, "urgency range is [%d:%d]",
                   FLUX_JOB_URGENCY_MIN, FLUX_JOB_URGENCY_MAX);
        goto inval;
    }
    if (!(job->cred.rolemask & FLUX_ROLE_OWNER)
        && job->urgency > FLUX_JOB_URGENCY_DEFAULT) {
        errprintf (error,
                   "only the instance owner can submit with urgency >%d",
                   FLUX_JOB_URGENCY_DEFAULT);
        goto inval;
    }
    if (!(job->cred.rolemask & FLUX_ROLE_OWNER)
        && (job->flags & FLUX_JOB_WAITABLE)) {
        errprintf (error,
                   "only the instance owner can submit with FLUX_JOB_WAITABLE");
        goto inval;
    }
    /* Validate jobspec signature, and unwrap(J) -> jobspec,  jobspecsz.
     * Userid claimed by signature must match authenticated job->cred.userid.
     * If not the instance owner, a strong signature is required
     * to give the IMP permission to launch processes on behalf of the user.
     */
#if HAVE_FLUX_SECURITY
    const void *jobspec;
    if (flux_sign_unwrap_anymech (security_context,
                                  job->J,
                                  (const void **)&jobspec,
                                  &job->jobspecsz,
                                  &mech_type,
                                  &userid_signer,
                                  FLUX_SIGN_NOVERIFY) < 0) {
        errprintf (error, "%s", flux_security_last_error (security_context));
        goto error;
    }
    if (!(job->jobspec = malloc (job->jobspecsz))) {
        errprintf (error, "out of memory while unwrapping job request");
        goto error;
    }
    memcpy (job->jobspec, jobspec, job->jobspecsz);
#else
    uint32_t userid_signer_u32;
    /* Simplified unwrap only understands mech=none.
     * Unlike flux-security version, returned payload must be freed,
     * and returned userid is a uint32_t.
     */
    if (sign_none_unwrap (job->J,
                          (void **)&job->jobspec,
                          &job->jobspecsz,
                          &userid_signer_u32) < 0) {
        errprintf (error, "could not unwrap jobspec: %s", strerror (errno));
        goto error;
    }
    mech_type = "none";
    userid_signer = userid_signer_u32;
#endif
    if (userid_signer != job->cred.userid) {
        errprintf (error,
                  "signer=%lu != requestor=%lu",
                  (unsigned long)userid_signer,
                  (unsigned long)job->cred.userid);
        errno = EPERM;
        goto error;
    }
    if (!(job->cred.rolemask & FLUX_ROLE_OWNER)
        && !strcmp (mech_type, "none")) {
        errprintf (error, "only instance owner can use sign-type=none");
        errno = EPERM;
        goto error;
    }
    if (!(job->jobspec_obj = json_loadb (job->jobspec,
                                         job->jobspecsz,
                                         0,
                                         &json_error))) {
        errprintf (error, "jobspec: invalid JSON: %s", json_error.text);
        goto inval;
    }
    return job;
inval:
    errno = EINVAL;
error:
    job_destroy (job);
    return NULL;
}

json_t *job_json_object (struct job *job, flux_error_t *error)
{
    json_error_t json_error;
    json_t *o;

    if (!(o = json_pack_ex (&json_error,
                            0,
                            "{s:O s:I s:i s:i s:i}",
                            "jobspec", job->jobspec_obj,
                            "userid", (json_int_t) job->cred.userid,
                            "rolemask", job->cred.rolemask,
                            "urgency", job->urgency,
                            "flags", job->flags))) {
        errprintf (error,
                   "Error creating JSON job object: %s", json_error.text);
        errno = EINVAL;
        return NULL;
    }
    return o;
}

// vi:tabstop=4 shiftwidth=4 expandtab
