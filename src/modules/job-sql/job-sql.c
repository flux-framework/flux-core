/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* TODO:
 * - delete row if 'invalidate' event is received
 * - delete row if job appears in a published 'job-purge-inactive' message
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libsqlite3/sqlite3.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

const char *sql_create_table =
    "CREATE TABLE jobs("
    "  id INT PRIMARY KEY,"
    "  eventlog JSON,"
    "  jobspec JSON,"
    "  R JSON"
    ");";

const char *sql_insert =
    "INSERT INTO jobs("
    "  id,"
    "  eventlog,"
    "  jobspec,"
    "  R"
    ") values (?1, ?2, ?3, ?4)";

const char *sql_update_eventlog =
    "UPDATE jobs"
    "  set eventlog = json_insert(eventlog, '$[#]', json(?2))"
    "where id = ?1";

const char *sql_update_jobspec =
    "UPDATE jobs"
    "  set jobspec = ?2"
    "where id = ?1";

const char *sql_update_R =
    "UPDATE jobs"
    "  set R = ?2"
    "where id = ?1";

const char *sql_delete =
    "DELETE from jobs where id = ?1";

struct job_sql_ctx {
    flux_t *h;
    sqlite3 *db;
    sqlite3_stmt *insert_stmt;
    sqlite3_stmt *update_eventlog_stmt;
    sqlite3_stmt *update_jobspec_stmt;
    sqlite3_stmt *update_R_stmt;
    sqlite3_stmt *delete_stmt;
    bool db_initialized;
    struct flux_msglist *deferred_requests;
    flux_msg_handler_t **handlers;
};

static int db_init (struct job_sql_ctx *ctx, flux_error_t *error)
{
    if (sqlite3_open_v2 ("jobdb",
                         &ctx->db,
                         (SQLITE_OPEN_READWRITE
                            | SQLITE_OPEN_CREATE
                            | SQLITE_OPEN_MEMORY),
                         NULL) != SQLITE_OK) {
        errprintf (error,
                   "db create: %s",
                   ctx->db ? sqlite3_errmsg (ctx->db) : "unknown error");
        return -1;
    }
    if (sqlite3_exec (ctx->db,
                      sql_create_table,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        errprintf (error, "db table create: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_insert,
                            -1,
                            &ctx->insert_stmt,
                            NULL) != SQLITE_OK) {
        errprintf (error, "db prepare insert: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_update_eventlog,
                            -1,
                            &ctx->update_eventlog_stmt,
                            NULL) != SQLITE_OK) {
        errprintf (error, "db prepare eventlog: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_update_jobspec,
                            -1,
                            &ctx->update_jobspec_stmt,
                            NULL) != SQLITE_OK) {
        errprintf (error, "db prepare jobspec: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_update_R,
                            -1,
                            &ctx->update_R_stmt,
                            NULL) != SQLITE_OK) {
        errprintf (error, "db prepare R: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_delete,
                            -1,
                            &ctx->delete_stmt,
                            NULL) != SQLITE_OK) {
        errprintf (error, "db prepare delete: %s", sqlite3_errmsg (ctx->db));
        return -1;
    }
    return 0;
}

// return -1 on failure, 0 on success, 1 on unique constraint error
static int db_insert (struct job_sql_ctx *ctx,
                      flux_jobid_t id,
                      json_t *events,
                      json_t *jobspec,
                      json_t *R,
                      flux_error_t *error)
{
    int rc = -1;
    int n;
    char *s;

    /* 1: id
     */
    if (sqlite3_bind_int64 (ctx->insert_stmt,
                            1,
                            id) != SQLITE_OK) {
        errprintf (error,
                   "db insert %s bind: %s",
                   idf58 (id),
                   sqlite3_errmsg (ctx->db));
        goto done;
    }
    /* 2: eventlog
     */
    if (!(s = json_dumps (events, JSON_COMPACT))) {
        errprintf (error, "error encoding eventlog");
        goto done;
    }
    if (sqlite3_bind_text (ctx->insert_stmt,
                           2,
                           s,
                           strlen (s),
                           free) != SQLITE_OK) {
        errprintf (error,
                   "db insert %s bind eventlog: %s",
                   idf58 (id),
                   sqlite3_errmsg (ctx->db));
        goto done;
    }
    /* 3: jobspec
     */
    if (jobspec) {
        if (!(s = json_dumps (jobspec, JSON_COMPACT))) {
            errprintf (error, "error encoding jobspec");
            goto done;
        }
        n = sqlite3_bind_text (ctx->insert_stmt, 3, s, strlen (s), free);
    }
    else
        n = sqlite3_bind_null (ctx->insert_stmt, 3);
    if (n != SQLITE_OK) {
        errprintf (error,
                   "db insert %s bind jobspec: %s",
                   idf58 (id),
                   sqlite3_errmsg (ctx->db));
        goto done;
    }
    /* 4: R
     */
    if (R) {
        if (!(s = json_dumps (R, JSON_COMPACT))) {
            errprintf (error, "error encoding R");
            goto done;
        }
        n = sqlite3_bind_text (ctx->insert_stmt, 4, s, strlen (s), free);
    }
    else
        n = sqlite3_bind_null (ctx->insert_stmt, 4);
    if (n != SQLITE_OK) {
        errprintf (error,
                   "db insert %s bind R: %s",
                   idf58 (id),
                   sqlite3_errmsg (ctx->db));
        goto done;
    }
    if ((n = sqlite3_step (ctx->insert_stmt)) != SQLITE_DONE) {
        errprintf (error,
                   "db insert %s: %s",
                   idf58 (id),
                   sqlite3_errmsg (ctx->db));
        if (n == SQLITE_CONSTRAINT)
            rc = 1;
        goto done;
    }
    rc = 0;
done:
    sqlite3_reset (ctx->insert_stmt);
    return rc;
}

static int db_update (struct job_sql_ctx *ctx,
                      flux_jobid_t id,
                      json_t *events,
                      json_t *jobspec,
                      json_t *R,
                      flux_error_t *error)
{
    int rc = -1;
    int n;

    /* If db_insert() succeeds, this is a new job and we're done.
     * If it returns 1, there was a constraint violation, and we must update.
     * Any other result is fatal.
     */
    if ((n = db_insert (ctx, id, events, jobspec, R, error)) == 0)
        return 0;
    if (n != 1)
        goto done;

    // job manager will never send multiple events except in backlog
    if (json_array_size (events) > 1) {
        errprintf (error, "db update: received multiple events in one update");
        goto done;
    }

    if (json_array_size (events) > 0) {
        char *s;
        if (!(s = json_dumps (json_array_get (events, 0), JSON_COMPACT))) {
            errprintf (error, "db update: error encoding event");
            goto done;
        }
        if (sqlite3_bind_int64 (ctx->update_eventlog_stmt, 1, id) != SQLITE_OK
            || sqlite3_bind_text (ctx->update_eventlog_stmt,
                                  2,
                                  s,
                                  strlen (s),
                                  free) != SQLITE_OK) {
            errprintf (error,
                       "db update eventlog %s bind: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            goto done;
        }
        if ((n = sqlite3_step (ctx->update_eventlog_stmt)) != SQLITE_DONE) {
            errprintf (error,
                       "db update eventlog %s: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            if (n == SQLITE_CONSTRAINT)
                rc = 1;
            goto done;
        }
    }
    if (jobspec) {
        char *s;
        if (!(s = json_dumps (jobspec, JSON_COMPACT))) {
            errprintf (error, "db update: error encoding jobspec");
            goto done;
        }
        if (sqlite3_bind_int64 (ctx->update_jobspec_stmt, 1, id) != SQLITE_OK
            || sqlite3_bind_text (ctx->update_jobspec_stmt,
                                  2,
                                  s,
                                  strlen (s),
                                  free) != SQLITE_OK) {
            errprintf (error,
                       "db update jobspec %s bind: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            goto done;
        }
        if ((n = sqlite3_step (ctx->update_jobspec_stmt)) != SQLITE_DONE) {
            errprintf (error,
                       "db update jobspec %s: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            if (n == SQLITE_CONSTRAINT)
                rc = 1;
            goto done;
        }
    }
    if (R) {
        char *s;
        if (!(s = json_dumps (R, JSON_COMPACT))) {
            errprintf (error, "db update: error encoding R");
            goto done;
        }
        if (sqlite3_bind_int64 (ctx->update_R_stmt, 1, id) != SQLITE_OK
            || sqlite3_bind_text (ctx->update_R_stmt,
                                  2,
                                  s,
                                  strlen (s),
                                  free) != SQLITE_OK) {
            errprintf (error,
                       "db update R %s bind: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            goto done;
        }
        if ((n = sqlite3_step (ctx->update_R_stmt)) != SQLITE_DONE) {
            errprintf (error,
                       "db update R %s: %s",
                       idf58 (id),
                       sqlite3_errmsg (ctx->db));
            if (n == SQLITE_CONSTRAINT)
                rc = 1;
            goto done;
        }
    }
    rc = 0;
done:
    sqlite3_reset (ctx->update_eventlog_stmt);
    sqlite3_reset (ctx->update_jobspec_stmt);
    sqlite3_reset (ctx->update_R_stmt);
    return rc;
}

static int query_result (void *arg, int ncols, char **cols, char **col_names)
{
    const flux_msg_t *msg = arg;
    struct job_sql_ctx *ctx = flux_msg_aux_get (msg, "ctx");
    json_t *row;

    if (!(row = json_object ()))
        goto error;
    for (int i = 0; i < ncols; i++) {
        json_t *val;
        if (!(val = json_loads (cols[i], JSON_DECODE_ANY, 0))
            || json_object_set_new (row, col_names[i], val) < 0) {
            json_decref (val);
            goto error;
        }
    }
    if (flux_respond_pack (ctx->h, msg, "O", row) < 0)
        flux_log_error (ctx->h, "error responding to query request");
    json_decref (row);
    return 0;
error:
    json_decref (row);
    return -1; // causes SQLITE_ABORT
}

static void query_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_sql_ctx *ctx = arg;
    const char *query;
    flux_error_t error;
    const char *errmsg = NULL;
    json_t *result = NULL;

    if (!ctx->db_initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s}",
                             "query", &query) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_aux_set (msg, "ctx", ctx, NULL) < 0)
        goto error;
    if (sqlite3_exec (ctx->db,
                      query,
                      query_result,
                      (void *)msg,
                      NULL) != SQLITE_OK) {
        errprintf (&error, "%s", sqlite3_errmsg (ctx->db));
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
        flux_log_error (h, "error responding to query request");
    json_decref (result);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to query request");
    json_decref (result);
}

/* sqlite3_exec() callback from count query.
 * On success, return 0 and set *arg to the count result.
 * On error, return -1 which causes sqlite3_exec() to fail with SQLITE_ABORT.
 */
static int stats_set_count (void *arg, int ncols, char **cols, char **col_names)
{
    int *result = arg;
    int count = 0;
    int rc = -1;

    if (ncols == 1) {
        errno = 0;
        count = strtoul (cols[0], NULL, 10);
        if (errno == 0) {
            *result = count;
            rc = 0;
        }
    }
    return rc; // returning -1 causes SQLITE_ABORT
}

static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_sql_ctx *ctx = arg;
    const char *errmsg = NULL;
    int count;

    if (sqlite3_exec (ctx->db,
                      "SELECT count(1) from jobs",
                      stats_set_count,
                      &count,
                      NULL) != SQLITE_OK) {
        errmsg = sqlite3_errmsg (ctx->db);
        errno = EINVAL;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:i}",
                           "object_count", count) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to stats-get request");
}

static void journal_continuation (flux_future_t *f, void *arg)
{
    struct job_sql_ctx *ctx = arg;
    flux_reactor_t *r = flux_future_get_reactor (f);
    flux_t *h = flux_future_get_flux (f);
    flux_jobid_t id;
    json_t *events;
    json_t *jobspec = NULL;
    json_t *R = NULL;

    if (flux_rpc_get_unpack (f,
                             "{s:I s:o s?o s?o}",
                             "id", &id,
                             "events", &events,
                             "jobspec", &jobspec,
                             "R", &R) < 0) {
        if (errno == ENODATA) {
            flux_log (h, LOG_INFO, "journal EOF");
            flux_reactor_stop (r);
            return;
        }
        goto error;
    }
    if (id == FLUX_JOBID_ANY) { // sentenel
        const flux_msg_t *msg;

        ctx->db_initialized = true;
        while ((msg = flux_msglist_pop (ctx->deferred_requests))) {
            if (flux_requeue (h, msg, FLUX_RQ_TAIL) < 0)
                flux_log_error (h, "error requeuing deferred request");
            flux_msg_decref (msg);
        }
    }
    else {
        flux_error_t error;
        if (ctx->db_initialized) {
            if (db_update (ctx, id, events, jobspec, R, &error) < 0) {
                flux_log (h, LOG_ERR, "%s: %s", idf58 (id), error.text);
                goto done;
            }
        }
        else {
            if (db_insert (ctx, id, events, jobspec, R, &error) < 0) {
                flux_log (h, LOG_ERR, "%s: %s", idf58 (id), error.text);
                goto error;
            }
        }
    }
done:
    flux_future_reset (f);
    return;
error:
    flux_reactor_stop_error (r);
}

static struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "job-sql.query",
        query_cb,
        0,
    },
    {   FLUX_MSGTYPE_REQUEST,
        "job-sql.stats-get",
        stats_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    struct job_sql_ctx *ctx;
    flux_future_t *f = NULL;
    flux_error_t error;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        goto done;
    ctx->h = h;
    if (db_init (ctx, &error) < 0) {
        flux_log (h, LOG_ERR, "db init: %s", error.text);
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0) {
        flux_log_error (h, "could not register message handlers");
        goto done;
    }
    if (!(f = flux_rpc_pack (h,
                             "job-manager.events-journal",
                             0,
                             FLUX_RPC_STREAMING,
                             "{s:b}",
                             "full", 1))
        || flux_future_then (f, -1, journal_continuation, ctx) < 0) {
        flux_log_error (h, "error sending job manager journal request");
        goto done;
    }
    if (!(ctx->deferred_requests = flux_msglist_create ())) {
        flux_log_error (h, "could not create deferred request list");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "module reactor stopped");
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    flux_msglist_destroy (ctx->deferred_requests);
    flux_msg_handler_delvec (ctx->handlers);
    if (ctx->insert_stmt)
        ERRNO_SAFE_WRAP (sqlite3_finalize, ctx->insert_stmt);
    if (ctx->update_eventlog_stmt)
        ERRNO_SAFE_WRAP (sqlite3_finalize, ctx->update_eventlog_stmt);
    if (ctx->update_jobspec_stmt)
        ERRNO_SAFE_WRAP (sqlite3_finalize, ctx->update_jobspec_stmt);
    if (ctx->update_R_stmt)
        ERRNO_SAFE_WRAP (sqlite3_finalize, ctx->update_R_stmt);
    if (ctx->delete_stmt)
        ERRNO_SAFE_WRAP (sqlite3_finalize, ctx->delete_stmt);
    if (ctx->db)
        ERRNO_SAFE_WRAP (sqlite3_close, ctx->db);
    free (ctx);
    return rc;
}

// vi:ts=4 sw=4 expandtab
