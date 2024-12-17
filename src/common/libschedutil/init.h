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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct schedutil_ctx schedutil_t;

/* schedutil_create() flags values
 */
#define SCHEDUTIL_FREE_NOLOOKUP 1 // now the default so this flag is ignored
#define SCHEDUTIL_HELLO_PARTIAL_OK 2

/* Create a handle for the schedutil convenience library.
 *
 * Used to track outstanding futures and register callbacks relevant for
 * schedulers and simulators.
 * Return NULL on error.
 */
schedutil_t *schedutil_create (flux_t *h,
                               int flags,
                               const struct schedutil_ops *ops,
                               void *arg);

/* Destroy the handle for the schedutil convenience library.
 *
 * Will automatically respond ENOSYS to any outstanding messages (e.g., free,
 * alloc).
 */
void schedutil_destroy (schedutil_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SCHEDUTIL_INIT_H */
