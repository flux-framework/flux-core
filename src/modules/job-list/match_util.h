/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_LIST_MATCH_UTIL_H
#define HAVE_JOB_LIST_MATCH_UTIL_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h> /* flux_error_t */
#include <jansson.h>

/* identical to czmq_destructor, czmq.h happens to define a
 * conflicting symbol we use */
typedef void (destructor_f) (void **item);

typedef int (*array_to_bitmask_f) (json_t *, flux_error_t *);

int array_to_states_bitmask (json_t *values, flux_error_t *errp);

int array_to_results_bitmask (json_t *values, flux_error_t *errp);

#endif /* !HAVE_JOB_LIST_MATCH_UTIL_H */
