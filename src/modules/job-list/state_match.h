/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_LIST_STATE_MATCH_H
#define HAVE_JOB_LIST_STATE_MATCH_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h> /* flux_error_t */
#include <jansson.h>

#include "job_data.h"

/*  Similar to list_constraint_create() but only cares about
 *  "states" operation and the potential for a consraint to
 *  return true given a job state..
 */
struct state_constraint *state_constraint_create (json_t *constraint,
                                                  flux_error_t *errp);

void state_constraint_destroy (struct state_constraint *constraint);

/* determines if a job in 'state' could potentially return true with
 * the given constraint.  'state' can be job state or virtual job state.
 */
bool state_match (int state, struct state_constraint *constraint);

#endif /* !HAVE_JOB_LIST_STATE_MATCH_H */
