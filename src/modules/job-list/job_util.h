/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_JOB_UTIL_H
#define _FLUX_JOB_LIST_JOB_UTIL_H

#include <flux/core.h>

#include "job_data.h"

typedef struct {
    char text[160];
} job_list_error_t;

void __attribute__((format (printf, 2, 3)))
seterror (job_list_error_t *errp, const char *fmt, ...);

json_t *job_to_json (struct job *job, json_t *attrs, job_list_error_t *errp);

#endif /* ! _FLUX_JOB_LIST_JOB_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
