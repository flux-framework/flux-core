/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_AUTH_H
#define _FLUX_JOB_LIST_AUTH_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <flux/core.h>
#include <jansson.h>

#include "match.h"
#include "job_data.h"

struct job_auth;

/* Create policy object; reads [access] private-mode from flux_get_conf(h).
 * Logs and returns NULL on config error.
 */
struct job_auth *job_auth_create (flux_t *h);

void job_auth_destroy (struct job_auth *auth);

/* Re-read [access] private-mode from conf. Returns 0, or -1 with errp set. */
int job_auth_config_reload (struct job_auth *auth,
                            const flux_conf_t *conf,
                            flux_error_t *errp);

/* Return true if request msg must be restricted to their own jobs:
 * i.e. private mode enabled and msg credential cannot be obtained or
 * lacks FLUX_ROLE_OWNER.
 */
bool job_auth_msg_restricted (struct job_auth *auth, const flux_msg_t *msg);

/* Produce an RFC31 constraint (JSON, new reference) to AND into a request's
 * constraint, or set *constraintp = NULL when no restriction applies (owner,
 * or private mode off). The built-in policy returns {"userid":[<uid>]}.
 * Returns 0 on success, -1 with errp set on error.
 */
int job_auth_constraint (struct job_auth *auth,
                         const flux_msg_t *msg,
                         json_t **constraintp,
                         flux_error_t *errp);

/* Check whether a single job is visible to msg under the current policy.
 * Returns 1 if visible, 0 if not, -1 with errp set on error. Implemented by
 * compiling the job_auth_constraint() result via match.h and running
 * job_match(); returns 1 immediately when not restricted.
 */
int job_auth_check_job (struct job_auth *auth,
                        struct match_ctx *mctx,
                        const flux_msg_t *msg,
                        const struct job *job,
                        flux_error_t *errp);

#endif /* !_FLUX_JOB_LIST_AUTH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
