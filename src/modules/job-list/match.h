/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_LIST_MATCH_H
#define HAVE_JOB_LIST_MATCH_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h> /* flux_error_t */
#include <jansson.h>

#include "job_data.h"

struct match_ctx {
    flux_t *h;
    uint64_t max_comparisons;
    uint32_t max_hostlist;
};

struct match_ctx *match_ctx_create (flux_t *h);

void match_ctx_destroy (struct match_ctx *mctx);

/*  Load and validate RFC 31 constraint spec 'constraint'.  'constraint'
 *  can be NULL to indicate a constraint that matches everything.
 *
 *  Returns a list constraint object if constraint is valid spec,
 *  Returns NULL with error in errp if errp != NULL.
 */
struct list_constraint *list_constraint_create (struct match_ctx *mctx,
                                                json_t *constraint,
                                                flux_error_t *errp);

void list_constraint_destroy (struct list_constraint *constraint);

/*  Return 1 if job matches constraints in RFC 31 constraint
 *   specification 'constraint'.  Return 0 if not.  Return -1
 *   on error.
 */
int job_match (const struct job *job,
               struct list_constraint *constraint,
               flux_error_t *errp);

int job_match_config_reload (struct match_ctx *mctx,
                             const flux_conf_t *conf,
                             flux_error_t *errp);

#endif /* !HAVE_JOB_LIST_MATCH_H */
