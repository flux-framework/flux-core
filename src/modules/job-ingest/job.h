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
    json_t *jobspec;    // jobspec modified after unwrap from J
};


void job_destroy (struct job *job);

struct job *job_create_from_request (const flux_msg_t *msg,
                                     void *security_context,
                                     flux_error_t *error);

json_t *job_json_object (struct job *job, flux_error_t *error);

#endif /* !_JOB_INGEST_JOB_H */

// vi:ts=4 sw=4 expandtab
