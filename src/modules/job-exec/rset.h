/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_RSET_H
#define HAVE_JOB_EXEC_RSET_H 1
#include <flux/idset.h>
#include <jansson.h>
#include <stdint.h>

struct resource_set;

struct resource_set *resource_set_create (const char *R, json_error_t *errp);

struct resource_set *resource_set_create_fromjson (json_t *R,
		                                   json_error_t *errp);

void resource_set_destroy (struct resource_set *rset);

json_t *resource_set_get_json (struct resource_set *rset);

const struct idset *resource_set_ranks (struct resource_set *rset);

uint32_t resource_set_nth_rank (struct resource_set *r, int n);

uint32_t resource_set_rank_index (struct resource_set *r, uint32_t rank);

/* Return the core idset string for broker rank `rank` from R_lite, or NULL
 * if the rank is not found or has no core assignment.  The returned pointer
 * is owned by the resource_set and is valid for the lifetime of `r`.
 */
const char *resource_set_rank_cores (struct resource_set *r, unsigned int rank);

double resource_set_starttime (struct resource_set *rset);

double resource_set_expiration (struct resource_set *rset);

void resource_set_update_expiration (struct resource_set *rset,
                                     double expiration);

#endif /* !HAVE_JOB_EXEC_RSET_H */


