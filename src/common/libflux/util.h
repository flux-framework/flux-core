/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_UTIL_H
#define _FLUX_CORE_UTIL_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum flux_process_scope {
    FLUX_PROCESS_SCOPE_NONE = 0,
    FLUX_PROCESS_SCOPE_SYSTEM_INSTANCE = 1,
    FLUX_PROCESS_SCOPE_INITIAL_PROGRAM = 2,
    FLUX_PROCESS_SCOPE_JOB = 3,
} flux_process_scope_t;

/* Retrieve information on the scope of a job
 *
 * NONE - process is not running under flux
 *
 * SYSTEM_INSTANCE - process is running under the system instance
 * - e.g. flux mini run my_process.sh
 *
 * INITIAL_PROGRAM - process is running as the initial program
 * of a user flux instance
 * - e.g. flux mini submit flux start my_process.sh
 * - e.g. flux start --test-size=1 my_process.sh
 *
 * JOB - process is running as a job in a non-system instance
 * - e.g. > flux mini submit flux start flux mini submit my_process.sh
 * - e.g. > flux mini alloc -N1
 *        > flux mini run my_process.sh
 *
 * Returns 0 on success, -1 on error
 */
int flux_get_process_scope (flux_process_scope_t *scope);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
