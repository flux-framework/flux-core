/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_INIT_H
#define _FLUX_SCHEDUTIL_INIT_H

#include <flux/core.h>

#include "ops.h"

typedef struct schedutil_ctx schedutil_t;

/* Create a handle for the schedutil conveinence library.
 *
 * Used to track outstanding futures and register callbacks relevant for
 * schedulers and simulators.
 * Return NULL on error.
 */
schedutil_t *schedutil_create (flux_t *h,
                               const struct schedutil_ops *ops,
                               void *arg);

/* Destory the handle for the schedutil conveinence library.
 *
 * Will automatically respond ENOSYS to any outstanding messages (e.g., free,
 * alloc).
 */
void schedutil_destroy (schedutil_t* ctx);

/* Kick off scheduler initialization protocols.
 * The reactor must run to make progress.
 */
int schedutil_init (schedutil_t *ctx);

#endif /* !_FLUX_SCHEDUTIL_INIT_H */
