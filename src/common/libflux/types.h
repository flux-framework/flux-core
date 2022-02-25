/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_TYPES_H
#define _FLUX_CORE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flux_free_f)(void *arg);

/*  Generic container for holding textual errors from selected libflux
 *   functions:
 */
typedef struct {
    char text[160];
} flux_error_t;

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_TYPES_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
