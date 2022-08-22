/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_JOB_H_
#define _JOB_INGEST_JOB_H_

#include <jansson.h>
#include <flux/core.h>

struct job {
    flux_jobid_t id;

    const flux_msg_t *msg; // submit request message
    const char *J;      // signed jobspec
    struct flux_msg_cred cred;    // submitting user's creds
    int urgency;        // requested job urgency
    int flags;          // submit flags

    char *jobspec;      // jobspec, not \0 terminated (unwrapped from signed)
    int jobspecsz;      // jobspec string length
    json_t *jobspec_obj;// jobspec in object form
                        //   N.B. after obj validation, environment is dropped
                        //   to reduce size, since job-manager doesn't need it
};


/* Free decoded jobspec after it has been transferred to the batch txn,
 * to conserve memory.
 */
void job_clean (struct job *job);

void job_destroy (struct job *job);

struct job *job_create_from_request (const flux_msg_t *msg,
                                     void *security_context,
                                     flux_error_t *error);

json_t *job_json_object (struct job *job, flux_error_t *error);

#endif /* !_JOB_INGEST_JOB_H */

// vi:ts=4 sw=4 expandtab
