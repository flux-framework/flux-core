/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* util.c - utility functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <stdarg.h>
#include <sqlite3.h>
#include <flux/core.h>
#include <assert.h>

#include "util.h"

void log_sqlite_error (struct job_db_ctx *dbctx, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    if (dbctx->db) {
        const char *errmsg = sqlite3_errmsg (dbctx->db);
        flux_log (dbctx->h,
                  LOG_ERR,
                  "%s: %s(%d)",
                  buf,
                  errmsg ? errmsg : "unknown error code",
                  sqlite3_extended_errcode (dbctx->db));
    }
    else
        flux_log (dbctx->h,
                  LOG_ERR,
                  "%s: unknown error, no sqlite3 handle",
                  buf);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
