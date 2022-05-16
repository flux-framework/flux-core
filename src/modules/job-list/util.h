/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_UTIL_H
#define _FLUX_JOB_LIST_UTIL_H

#include <stdarg.h>

#include "job_db.h"
#include "job_data.h"

void __attribute__((format (printf, 2, 3)))
log_sqlite_error (struct job_db_ctx *dbctx, const char *fmt, ...);

#endif /* ! _FLUX_JOB_LIST_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
