/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_db: support storing inactive jobs to db */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <flux/core.h>
#include <jansson.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/monotime.h"

#include "job_db.h"
#include "job_util.h"
#include "util.h"

#define BUSY_TIMEOUT_DEFAULT 50
#define BUFSIZE              1024

/* N.B. "state" is always INACTIVE, but added in case of future changes */

const char *sql_create_table = "CREATE TABLE if not exists jobs("
                               "  id CHAR(16) PRIMARY KEY,"
                               "  userid INT,"
                               "  name TEXT,"
                               "  queue TEXT,"
                               "  state INT,"
                               "  result INT,"
                               "  nodelist TEXT,"
                               "  ranks TEXT,"
                               "  t_submit REAL,"
                               "  t_depend REAL,"
                               "  t_run REAL,"
                               "  t_cleanup REAL,"
                               "  t_inactive REAL,"
                               "  jobdata JSON,"
                               "  eventlog TEXT,"
                               "  jobspec JSON,"
                               "  R JSON"
    ");";

const char *sql_store =      \
    "INSERT INTO jobs"       \
    "("                      \
    "  id,"                  \
    "  userid,"              \
    "  name,"                \
    "  queue,"               \
    "  state,"               \
    "  result,"              \
    "  nodelist,"            \
    "  ranks,"               \
    "  t_submit,"            \
    "  t_depend,"            \
    "  t_run,"               \
    "  t_cleanup,"           \
    "  t_inactive,"          \
    "  jobdata,"             \
    "  eventlog,"            \
    "  jobspec,"             \
    "  R"                    \
    ") values ("             \
    "  ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17 " \
    ")";

void job_db_ctx_destroy (struct job_db_ctx *ctx)
{
    if (ctx) {
        free (ctx->dbpath);
        if (ctx->store_stmt) {
            if (sqlite3_finalize (ctx->store_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize store_stmt");
        }
        if (ctx->db) {
            if (sqlite3_close (ctx->db) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite3_close");
        }
        if (ctx->handlers)
            flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
    }
}

static struct job_db_ctx * job_db_ctx_create (flux_t *h)
{
    struct job_db_ctx *ctx = calloc (1, sizeof (*ctx));

    if (!ctx) {
        flux_log_error (h, "job_db_ctx_create");
        goto error;
    }

    ctx->h = h;
    ctx->busy_timeout = BUSY_TIMEOUT_DEFAULT;

    return ctx;
 error:
    job_db_ctx_destroy (ctx);
    return (NULL);
}

static unsigned long long get_file_size (const char *path)
{
    struct stat sb;

    if (stat (path, &sb) < 0)
        return 0;
    return sb.st_size;
}

static void db_stats_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct job_db_ctx *ctx = arg;

    if (flux_respond_pack (h,
                           msg,
                           "{s:I s:{s:i s:f s:f s:f s:f}}",
                           "dbfile_size", get_file_size (ctx->dbpath),
                           "store",
                             "count", tstat_count (&ctx->sqlstore),
                             "min", tstat_min (&ctx->sqlstore),
                             "max", tstat_max (&ctx->sqlstore),
                             "mean", tstat_mean (&ctx->sqlstore),
                             "stddev", tstat_stddev (&ctx->sqlstore)) < 0)
        flux_log_error (h, "error responding to db-stats request");
    return;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-list.db-stats", db_stats_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static int get_max_inactive(struct job_db_ctx *ctx)
{
    char *inactive_max_query = "SELECT MAX(t_inactive) FROM jobs";
    sqlite3_stmt *res = NULL;
    int save_errno, rv = -1;

    if (sqlite3_prepare_v2 (ctx->db,
                            inactive_max_query,
                            -1,
                            &res,
                            0) != SQLITE_OK) {
        log_sqlite_error (ctx, "sqlite3_prepare_v2");
        goto error;
    }

    while (sqlite3_step (res) == SQLITE_ROW) {
        const char *s = (const char *)sqlite3_column_text (res, 0);
        if (s) {
            char *endptr;
            double d;
            errno = 0;
            d = strtod (s, &endptr);
            if (errno || *endptr != '\0')
                goto error;
            ctx->initial_max_inactive = d;
            break;
        }
    }

    rv = 0;
error:
    save_errno = errno;
    sqlite3_finalize (res);
    errno = save_errno;
    return rv;
}

int job_db_init (struct job_db_ctx *ctx)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    char buf[1024];
    int rc = -1;

    if (sqlite3_open_v2 (ctx->dbpath, &ctx->db, flags, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "opening %s", ctx->dbpath);
        goto error;
    }

    if (sqlite3_exec (ctx->db,
                      "PRAGMA journal_mode=WAL",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'journal_mode' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA synchronous=NORMAL",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'synchronous' pragma");
        goto error;
    }
    snprintf (buf, 1024, "PRAGMA busy_timeout=%u;", ctx->busy_timeout);
    if (sqlite3_exec (ctx->db,
                      buf,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'busy_timeout' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      sql_create_table,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "creating object table");
        goto error;
    }

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_store,
                            -1,
                            &ctx->store_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing store stmt");
        goto error;
    }

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &ctx->handlers) < 0) {
        flux_log_error (ctx->h, "flux_msg_handler_addvec");
        goto error;
    }

    if (get_max_inactive (ctx) < 0)
        goto error;

    rc = 0;
error:
    return rc;
}

int job_db_store (struct job_db_ctx *ctx, struct job *job)
{
    json_t *o = NULL;
    flux_error_t err;
    char *job_str = NULL;
    char *jobspec = NULL;
    char *R = NULL;
    char idbuf[64];
    struct timespec t0;
    int rv = -1;

    /* when job-list is initialized from the journal, we could
     * re-store duplicate entries into the db.  Do not do this if the
     * t_inactive is less than the max we read from the db upon module
     * initialization
     *
     * Note, small chance of floating point rounding errors here, but
     * if 1 job is added twice to the DB, we can live with it.
     */
    if (job->t_inactive <= ctx->initial_max_inactive)
        return 0;

    monotime (&t0);

    snprintf (idbuf, 64, "%llu", (unsigned long long)job->id);
    if (sqlite3_bind_text (ctx->store_stmt,
                           1,
                           idbuf,
                           strlen (idbuf),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding id");
        goto out;
    }
    if (sqlite3_bind_int (ctx->store_stmt,
                          2,
                          job->userid) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding userid");
        goto out;
    }
    /* N.B. name can be NULL.  sqlite_bind_text() same as
     * sqlite_bind_null() if pointer is NULL */
    if (sqlite3_bind_text (ctx->store_stmt,
                           3,
                           job->name,
                           job->name ? strlen (job->name) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding job name");
        goto out;
    }
    /* N.B. queue can be NULL.  sqlite_bind_text() same as
     * sqlite_bind_null() if pointer is NULL */
    if (sqlite3_bind_text (ctx->store_stmt,
                           4,
                           job->queue,
                           job->queue ? strlen (job->queue) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding job queue");
        goto out;
    }
    if (sqlite3_bind_int (ctx->store_stmt,
                          5,
                          job->state) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding state");
        goto out;
    }
    if (sqlite3_bind_int (ctx->store_stmt,
                          6,
                          job->result) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding result");
        goto out;
    }
    /* N.B. nodelist can be NULL.  sqlite_bind_text() same as
     * sqlite_bind_null() if pointer is NULL */
    if (sqlite3_bind_text (ctx->store_stmt,
                           7,
                           job->nodelist,
                           job->nodelist ? strlen (job->nodelist) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding job nodelist");
        goto out;
    }
    /* N.B. ranks can be NULL.  sqlite_bind_text() same as
     * sqlite_bind_null() if pointer is NULL */
    if (sqlite3_bind_text (ctx->store_stmt,
                           8,
                           job->ranks,
                           job->ranks ? strlen (job->ranks) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding job nodelist");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             9,
                             job->t_submit) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_submit");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             10,
                             job->t_depend) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_depend");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             11,
                             job->t_run) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_run");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             12,
                             job->t_cleanup) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_cleanup");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             13,
                             job->t_inactive) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_inactive");
        goto out;
    }
    if (!(o = job_to_json_dbdata (job, &err)))
        goto out;
    if (!(job_str = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           14,
                           job_str,
                           strlen (job_str),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding jobdata");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           15,
                           job->eventlog,
                           strlen (job->eventlog),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding eventlog");
        goto out;
    }
    if (!(jobspec = json_dumps (job->jobspec, 0))) {
        flux_log_error (ctx->h, "json_dumps jobspec");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           16,
                           jobspec,
                           strlen (jobspec),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding jobspec");
        goto out;
    }
    if (job->R) {
        if (!(R = json_dumps (job->R, 0))) {
            flux_log_error (ctx->h, "json_dumps R");
            goto out;
        }
    }
    /* N.B. R can be NULL.  sqlite_bind_text() same as
     * sqlite_bind_null() if pointer is NULL */
    if (sqlite3_bind_text (ctx->store_stmt,
                           17,
                           R,
                           R ? strlen (R) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding R");
        goto out;
    }
    while (sqlite3_step (ctx->store_stmt) != SQLITE_DONE) {
        /* due to rounding errors in sqlite, duplicate entries could be
         * written out on occasion leading to a SQLITE_CONSTRAINT error.
         * We accept this and move on.
         */
        int errcode = sqlite3_errcode (ctx->db);
        if (errcode == SQLITE_CONSTRAINT)
            break;
        else if (errcode == SQLITE_BUSY) {
            /* In the rare case this cannot complete within the normal
             * busytimeout, we elect to spin till it completes.  This
             * may need to be revisited in the future: */
            flux_log (ctx->h, LOG_DEBUG, "%s: BUSY", __FUNCTION__);
            usleep (1000);
            continue;
        }
        else {
            log_sqlite_error (ctx, "store: executing stmt");
            goto out;
        }
    }

    tstat_push (&ctx->sqlstore, monotime_since (t0));

    rv = 0;
out:
    sqlite3_reset (ctx->store_stmt);
    json_decref (o);
    free (job_str);
    free (jobspec);
    free (R);
    return rv;
}

static int process_config (struct job_db_ctx *ctx)
{
    flux_error_t err;
    const char *dbpath = NULL;
    const char *busytimeout = NULL;

    if (flux_conf_unpack (flux_get_conf (ctx->h),
                          &err,
                          "{s?{s?s s?s}}",
                          "job-list",
                            "dbpath", &dbpath,
                            "busytimeout", &busytimeout) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "error reading db config: %s",
                  err.text);
        return -1;
    }

    if (dbpath) {
        if (!(ctx->dbpath = strdup (dbpath)))
            flux_log_error (ctx->h, "dbpath not configured");
    }
    else {
        const char *dbdir = flux_attr_get (ctx->h, "statedir");
        if (dbdir) {
            if (asprintf (&ctx->dbpath, "%s/job-db.sqlite", dbdir) < 0) {
                flux_log_error (ctx->h, "asprintf");
                return -1;
            }
        }
    }
    if (busytimeout) {
        double tmp;
        if (fsd_parse_duration (busytimeout, &tmp) < 0)
            flux_log_error (ctx->h, "busytimeout not configured");
        else
            ctx->busy_timeout = (int)(1000 * tmp);
    }

    return 0;
}

struct job_db_ctx * job_db_setup (flux_t *h, int ac, char **av)
{
    struct job_db_ctx *ctx = job_db_ctx_create (h);

    if (!ctx)
        return NULL;

    if (process_config (ctx) < 0)
        goto done;

    if (!ctx->dbpath) {
        errno = ENOTBLK;
        goto done;
    }

    if (job_db_init (ctx) < 0)
        goto done;

    return ctx;

done:
    job_db_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
