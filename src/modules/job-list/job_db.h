/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_DB_H
#define _FLUX_JOB_DB_H

#include <flux/core.h>
#include <sqlite3.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/tstat.h"

#include "job_data.h"

struct job_db_ctx {
    flux_t *h;
    char *dbpath;
    unsigned int busy_timeout;
    sqlite3 *db;
    sqlite3_stmt *store_stmt;
    flux_msg_handler_t **handlers;
    tstat_t sqlstore;
    double initial_max_inactive; /* when db initially loaded */
};

struct job_db_ctx *job_db_setup (flux_t *h, int ac, char **av);

void job_db_ctx_destroy (struct job_db_ctx *ctx);

int job_db_store (struct job_db_ctx *ctx, struct job *job);

#endif /* _FLUX_JOB_DB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

