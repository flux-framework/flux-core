/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job-archive: archive job data service for flux */

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

#include "src/common/libsqlite3/sqlite3.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libjob/idf58.h"

#define BUSY_TIMEOUT_DEFAULT 50
#define BUFSIZE              1024

const char *sql_create_table = "CREATE TABLE if not exists jobs("
                               "  id CHAR(16) PRIMARY KEY,"
                               "  userid INT,"
                               "  ranks TEXT,"
                               "  t_submit REAL,"
                               "  t_run REAL,"
                               "  t_cleanup REAL,"
                               "  t_inactive REAL,"
                               "  eventlog TEXT,"
                               "  jobspec TEXT,"
                               "  R TEXT"
    ");";

const char *sql_store =                               \
    "INSERT INTO jobs"                                \
    "("                                               \
    "  id,userid,ranks,"                              \
    "  t_submit,t_run,t_cleanup,t_inactive,"          \
    "  eventlog,jobspec,R"                            \
    ") values ("                                      \
    "  ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10"       \
    ")";

const char *sql_since = "SELECT MAX(t_inactive) FROM jobs;";

struct job_archive_ctx {
    flux_t *h;
    double period;
    char *dbpath;
    unsigned int busy_timeout;
    flux_watcher_t *w;
    sqlite3 *db;
    sqlite3_stmt *store_stmt;
    double since;
    int kvs_lookup_count;
    tstat_t sqlstore;
};

static void log_sqlite_error (struct job_archive_ctx *ctx, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    if (ctx->db) {
        const char *errmsg = sqlite3_errmsg (ctx->db);
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s: %s(%d)",
                  buf,
                  errmsg ? errmsg : "unknown error code",
                  sqlite3_extended_errcode (ctx->db));
    }
    else
        flux_log (ctx->h, LOG_ERR, "%s: unknown error, no sqlite3 handle", buf);
}

static void job_archive_ctx_destroy (struct job_archive_ctx *ctx)
{
    if (ctx) {
        free (ctx->dbpath);
        flux_watcher_destroy (ctx->w);
        if (ctx->store_stmt) {
            if (sqlite3_finalize (ctx->store_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize store_stmt");
        }
        if (ctx->db) {
            if (sqlite3_close (ctx->db) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite3_close");
        }
        free (ctx);
    }
}

static struct job_archive_ctx * job_archive_ctx_create (flux_t *h)
{
    struct job_archive_ctx *ctx = calloc (1, sizeof (*ctx));

    if (!ctx) {
        flux_log_error (h, "job_archive_ctx_create");
        goto error;
    }

    ctx->h = h;
    ctx->period = 0.0;
    ctx->busy_timeout = BUSY_TIMEOUT_DEFAULT;

    return ctx;
 error:
    job_archive_ctx_destroy (ctx);
    return (NULL);
}

int since_cb (void *arg, int argc, char **argv, char **colname)
{
    struct job_archive_ctx *ctx = arg;
    char *endptr;
    double tmp;

    if (argv[0] == NULL)
        return 0;

    errno = 0;
    tmp = strtod (argv[0], &endptr);
    if (errno || *endptr != '\0') {
        flux_log_error (ctx->h, "%s: invalid t_inactive", __FUNCTION__);
        return -1;
    }
    if (tmp > ctx->since)
        ctx->since = tmp;
    return 0;
}

int job_archive_since_init (struct job_archive_ctx *ctx)
{
    char *errmsg = NULL;

    if (sqlite3_exec (ctx->db,
                      sql_since,
                      since_cb,
                      ctx,
                      &errmsg) != SQLITE_OK) {
        log_sqlite_error (ctx, "%s: getting max since value: %s",
                          __FUNCTION__, errmsg);
        return -1;
    }

    return 0;
}

int job_archive_init (struct job_archive_ctx *ctx)
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

    if (job_archive_since_init (ctx) < 0)
        goto error;

    rc = 0;
error:
    return rc;
}

int append_key (struct job_archive_ctx *ctx, json_t *keys, const char *key)
{
    json_t *s = NULL;
    int rc = -1;

    if (!(s = json_string (key))) {
        flux_log_error (ctx->h, "%s: json_string", __FUNCTION__);
        goto error;
    }
    if (json_array_append_new (keys, s) < 0) {
        flux_log_error (ctx->h, "%s: json_array_append_new", __FUNCTION__);
        goto error;
    }
    return 0;
error:
    json_decref (s);
    return rc;
}

void json_decref_wrapper (void *arg)
{
    json_decref ((json_t *)arg);
}

void job_info_lookup_continuation (flux_future_t *f, void *arg)
{
    struct job_archive_ctx *ctx = arg;
    json_t *job;
    flux_jobid_t id;
    uint32_t userid;
    const char *ranks = NULL;
    double t_submit = 0.0;
    double t_run = 0.0;
    double t_cleanup = 0.0;
    double t_inactive = 0.0;
    const char *eventlog = NULL;
    const char *jobspec = NULL;
    const char *R = NULL;
    char idbuf[64];
    struct timespec t0;
    json_error_t error;

    monotime (&t0);

    if (flux_rpc_get_unpack (f, "{s:s s:s s?s}",
                             "eventlog", &eventlog,
                             "jobspec", &jobspec,
                             "R", &R) < 0) {
        flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto out;
    }

    if (!(job = flux_future_aux_get (f, "job"))) {
        flux_log_error (ctx->h, "%s: flux_future_aux_get", __FUNCTION__);
        goto out;
    }

    if (json_unpack_ex (job, &error, 0, "{s:I}", "id", &id) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: can't parse job id: %s",
                  __FUNCTION__, error.text);
        goto out;
    }

    if (json_unpack_ex (job, &error, 0,
                        "{s:i s?s s:f s?f s?f s:f}",
                        "userid", &userid,
                        "ranks", &ranks,
                        "t_submit", &t_submit,
                        "t_run", &t_run,
                        "t_cleanup", &t_cleanup,
                        "t_inactive", &t_inactive) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: parse job %s error: %s",
                  __FUNCTION__, idf58 (id), error.text);
        goto out;
    }

    snprintf (idbuf, 64, "%llu", (unsigned long long)id);
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
                          userid) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding userid");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           3,
                           ranks ? ranks: "",
                           ranks ? strlen (ranks) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding ranks");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             4,
                             t_submit) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_submit");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             5,
                             t_run) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_run");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             6,
                             t_cleanup) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_cleanup");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             7,
                             t_inactive) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_inactive");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           8,
                           eventlog,
                           strlen (eventlog),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding eventlog");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           9,
                           jobspec,
                           strlen (jobspec),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding jobspec");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           10,
                           R ? R: "",
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
        int err = sqlite3_errcode (ctx->db);
        if (err == SQLITE_CONSTRAINT)
            break;
        else if (err == SQLITE_BUSY) {
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

    if (t_inactive > ctx->since)
        ctx->since = t_inactive;

    tstat_push (&ctx->sqlstore, monotime_since (t0));

out:
    sqlite3_reset (ctx->store_stmt);
    flux_future_destroy (f);
    if (ctx->kvs_lookup_count
        && (--(ctx->kvs_lookup_count)) == 0) {
        flux_timer_watcher_reset (ctx->w, ctx->period, 0.);
        flux_watcher_start (ctx->w);
    }
}

int job_info_lookup (struct job_archive_ctx *ctx, json_t *job)
{
    const char *topic = "job-info.lookup";
    flux_future_t *f = NULL;
    flux_jobid_t id;
    json_t *keys = NULL;
    double t_run = 0.0;

    if (json_unpack (job, "{s:I s?f}", "id", &id, "t_run", &t_run) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: parse t_run", __FUNCTION__);
        goto error;
    }

    if (!(keys = json_array ())) {
        flux_log_error (ctx->h, "%s: json_array", __FUNCTION__);
        goto error;
    }
    if (append_key (ctx, keys, "eventlog") < 0)
        goto error;
    if (append_key (ctx, keys, "jobspec") < 0)
        goto error;
    if (t_run > 0.0) {
        if (append_key (ctx, keys, "R") < 0)
            goto error;
    }

    if (!(f = flux_rpc_pack (ctx->h, topic, FLUX_NODEID_ANY, 0,
                             "{s:I s:O s:i}",
                             "id", id,
                             "keys", keys,
                             "flags", 0))) {
        flux_log_error (ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }
    if (flux_future_then (f,
                          -1.,
                          job_info_lookup_continuation,
                          ctx) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }
    if (flux_future_aux_set (f,
                             "job",
                             json_incref (job),
                             json_decref_wrapper) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_aux_set", __FUNCTION__);
        goto error;
    }

    json_decref (keys);
    ctx->kvs_lookup_count++;
    return 0;

error:
    flux_future_destroy (f);
    json_decref (keys);
    return -1;
}

void job_list_inactive_continuation (flux_future_t *f, void *arg)
{
    struct job_archive_ctx *ctx = arg;
    json_t *jobs;
    size_t index;
    json_t *value;

    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        return;
    }
    json_array_foreach (jobs, index, value) {
        if (job_info_lookup (ctx, value) < 0)
            break;
    }
    /* If no new inactive jobs, still need to reset timer */
    if (!ctx->kvs_lookup_count) {
        flux_timer_watcher_reset (ctx->w, ctx->period, 0.);
        flux_watcher_start (ctx->w);
    }
    flux_future_destroy (f);
}

void job_archive_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct job_archive_ctx *ctx = arg;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (ctx->h,
                             "job-list.list",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i s:f s:[ssssss] s:{s:[i]}}",
                             "max_entries", 0,
                             "since", ctx->since,
                             "attrs",
                               "userid",
                               "ranks",
                               "t_submit",
                               "t_run",
                               "t_cleanup",
                               "t_inactive",
                             "constraint",
                               "states", FLUX_JOB_STATE_INACTIVE))) {
        flux_log_error (ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        return;
    }
    if (flux_future_then (f, -1, job_list_inactive_continuation, ctx) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        return;
    }
}

void stats_get_cb (flux_t *h,
                   flux_msg_handler_t *mh,
                   const flux_msg_t *msg,
                   void *arg)
{
    struct job_archive_ctx *ctx = arg;

    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:f s:f s:f s:f}",
                           "count", tstat_count (&ctx->sqlstore),
                           "min", tstat_min (&ctx->sqlstore),
                           "max", tstat_max (&ctx->sqlstore),
                           "mean", tstat_mean (&ctx->sqlstore),
                           "stddev", tstat_stddev (&ctx->sqlstore)) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
}

static int process_config (struct job_archive_ctx *ctx)
{
    flux_error_t err;
    const char *period = NULL;
    const char *dbpath = NULL;
    const char *busytimeout = NULL;

    if (flux_conf_unpack (flux_get_conf (ctx->h),
                          &err,
                          "{s?{s?s s?s s?s}}",
                          "archive",
                            "period", &period,
                            "dbpath", &dbpath,
                            "busytimeout", &busytimeout) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "error reading archive config: %s",
                  err.text);
        return -1;
    }

    if (period) {
        if (fsd_parse_duration (period, &ctx->period) < 0)
            flux_log_error (ctx->h, "period not configured");
    }
    if (dbpath) {
        if (!(ctx->dbpath = strdup (dbpath)))
            flux_log_error (ctx->h, "dbpath not configured");
    }
    else {
        const char *dbdir;
        if (!(dbdir = flux_attr_get (ctx->h, "statedir"))) {
            flux_log_error (ctx->h, "statedir not set");
            return -1;
        }

        if (asprintf (&ctx->dbpath, "%s/job-archive.sqlite", dbdir) < 0) {
            flux_log_error (ctx->h, "asprintf");
            return -1;
        }
    }
    if (busytimeout) {
        double tmp;
        if (fsd_parse_duration (busytimeout, &tmp) < 0)
            flux_log_error (ctx->h, "busytimeout not configured");
        else
            ctx->busy_timeout = (int)(1000 * tmp);
    }

    /* period is required to be set */
    if (ctx->period == 0.0) {
        flux_log_error (ctx->h, "period not set");
        return -1;
    }
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-archive.stats-get",
      stats_get_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int ac, char **av)
{
    struct job_archive_ctx *ctx = job_archive_ctx_create (h);
    flux_msg_handler_t **handlers = NULL;
    int rc = -1;

    if (!ctx)
        return -1;

    if (process_config (ctx) < 0)
        goto done;

    if (job_archive_init (ctx) < 0)
        goto done;

    if ((ctx->w = flux_timer_watcher_create (flux_get_reactor (h),
                                             ctx->period,
                                             0.,
                                             job_archive_cb,
                                             ctx)) < 0) {
        flux_log_error (h, "flux_timer_watcher_create");
        goto done;
    }

    flux_watcher_start (ctx->w);

    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }

    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");

done:
    flux_msg_handler_delvec (handlers);
    job_archive_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
