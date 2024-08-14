/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux cron: cron-like service for flux */

/*
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>
#include <ctype.h>
#include <signal.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"
#include "ccan/str/str.h"

#include "task.h"
#include "entry.h"
#include "types.h"

struct cron_ctx {
    flux_t *               h;
    uint64_t               next_id;         /* Id for next cron entry        */
    char *                 sync_event;      /* If set, sync entries to event */
    flux_msg_handler_t *   mh;              /* sync event message handler    */
    zlist_t    *           entries;
    zlist_t    *           deferred;        /* list of deferred entries      */
    double                 last_sync;       /* timestamp of last sync event  */
    double                 sync_epsilon;    /* allow tasks to run for this
                                               number of seconds after last-
                                               sync before deferring         */
    char *                 cwd;             /* cwd to avoid constant lookups */
};

/**************************************************************************
 * Forward declarations
 */
static cron_entry_t *cron_entry_create (cron_ctx_t *ctx, const flux_msg_t *msg);
static void cron_entry_destroy (cron_entry_t *e);
static int cron_entry_stop (cron_entry_t *e);
static void cron_entry_finished_handler (flux_t *h,
                                         cron_task_t *t,
                                         void *arg);
static void cron_entry_io_cb (flux_t *h,
                              cron_task_t *t,
                              void *arg,
                              bool is_stderr,
                              const char *data, int datalen);
static int cron_entry_run_task (cron_entry_t *e);
static int cron_entry_defer (cron_entry_t *e);

/**************************************************************************/
/* Public apis
 */
void *cron_entry_type_data (cron_entry_t *e)
{
    return e->data;
}

double get_timestamp (void)
{
    struct timespec tm;
    clock_gettime (CLOCK_REALTIME, &tm);
    return ((double) tm.tv_sec + (tm.tv_nsec/1.0e9));
}

static int cron_entry_run_task (cron_entry_t *e)
{
    flux_t *h = e->ctx->h;
    if (cron_task_run (e->task, e->rank, e->command, e->cwd, e->env) < 0) {
        flux_log_error (h, "cron-%ju: cron_task_run", e->id);
        /* Run "finished" handler since this task is done */
        cron_entry_finished_handler (h, e->task, e);
        return (-1);
    }
    e->stats.lastrun = get_timestamp ();
    return (0);
}

static int cron_entry_increment (cron_entry_t *e)
{
    ++e->stats.total;
    return ++e->stats.count;
}

int cron_entry_schedule_task (cron_entry_t *e)
{
    flux_t *h = e->ctx->h;

    /* Refuse to run more than one task at once
     */
    if (e->task) {
        flux_log (h,
                  LOG_INFO,
                  "cron-%ju: %s: task still running or scheduled",
                  e->id,
                  e->name);
        return (0);
    }
    if (!(e->task = cron_task_new (h, cron_entry_finished_handler, e))) {
        flux_log_error (h, "cron_task_create");
        return -1;
    }
    cron_task_on_io (e->task, cron_entry_io_cb);
    if (e->timeout >= 0.0)
        cron_task_set_timeout (e->task, e->timeout, NULL);

    /*   if we've reached our (non-zero) repeat count, prematurely stop
     *     the current entry (i.e. remove it from event loop, but leave
     *     it in ctx->entries so it can be listed/queried)
     */
    if (cron_entry_increment (e) == e->repeat)
        cron_entry_stop (e);

    return cron_entry_defer (e);
}


/**************************************************************************/

static void cron_entry_io_cb (flux_t *h, cron_task_t *t, void *arg,
    bool is_stderr, const char *data, int datalen)
{
    cron_entry_t *e = arg;
    int level = is_stderr ? LOG_ERR : LOG_INFO;
    flux_log (h,
              level,
              "cron-%ju[%s]: rank=%d: command=\"%s\": \"%s\"",
              e->id,
              e->name,
              e->rank,
              e->command,
              data);
}

/* Push task t onto the front of the completed tasks list for
 *  entry e. If the list has grown past completed-task-size, then drop the
 *  tail task on the list.
 */
int cron_entry_push_finished_task (cron_entry_t *e, struct cron_task *t)
{
    if (zlist_push (e->finished_tasks, t) < 0)
        return (-1);
    if (zlist_size (e->finished_tasks) > e->task_history_count) {
        struct cron_task *tdel = zlist_tail (e->finished_tasks);
        if (tdel) {
            zlist_remove (e->finished_tasks, tdel);
            cron_task_destroy (tdel);
        }
    }
    return (0);
}

static void cron_entry_failure (cron_entry_t *e)
{
    e->stats.failure++;
    e->stats.failcount++;
    if (e->stop_on_failure && e->stats.failcount >= e->stop_on_failure) {
        flux_log (e->ctx->h,
                  LOG_ERR,
                  "cron-%ju: exceeded failure limit of %d. stopping",
                  e->id,
                  e->stop_on_failure);
        cron_entry_stop (e);
    }
}

static void cron_entry_finished_handler (flux_t *h, cron_task_t *t, void *arg)
{
    cron_entry_t *e = arg;

    if (streq (cron_task_state (t), "Exec Failure")) {
        flux_log_error (h, "cron-%ju: failed to run %s", e->id, e->command);
        cron_entry_failure (e);
    }
    else if (streq (cron_task_state (t), "Rexec Failure")) {
        flux_log_error (h, "cron-%ju: failure running %s", e->id, e->command);
        cron_entry_failure (e);
    }
    else if (cron_task_status (t) != 0) {
        flux_log (h,
                  LOG_ERR, "cron-%ju: \"%s\": Failed: %s",
                  e->id,
                  e->command,
                  cron_task_state (t));
        cron_entry_failure (e);
    }
    else
        e->stats.success++;

    /*
     *  Push the completed task onto the completed task list and
     *   drop a task if needed. Reset e->task to NULL since there
     *   is currently no active task.
     */
    if (cron_entry_push_finished_task (e, t) < 0)
        return;
    e->task = NULL;

    /*
     *   If destruction of this cron entry has been requested, complete
     *    the destroy here.
     */
    if (e->destroyed)
        cron_entry_destroy (e);
}

static int cron_entry_stop (cron_entry_t *e)
{
    if (!e->data || e->stopped) {
        errno = EINVAL;
        return (-1);
    }
    e->ops.stop (e->data);
    e->stopped = 1;
    return (0);
}

/*
 * Callback used to stop a cron entry safely.
 */
static void entry_stop_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    cron_entry_stop (arg);
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
}

/* Stop cron entry `e` "safely" by waiting until the next
 *  "prepare" callback. Temporary watcher created here will be
 *  destroyed within prepare_cb.
 */
int cron_entry_stop_safe (cron_entry_t *e)
{
    flux_reactor_t *r = flux_get_reactor (e->ctx->h);
    flux_watcher_t *w = flux_prepare_watcher_create (r,
                                                     entry_stop_cb,
                                                     e);
    if (!w)
        return (-1);
    flux_watcher_start (w);
    return (0);
}

static int cron_entry_start (cron_entry_t *e)
{
    if (!e->data || !e->stopped) {
        errno = EINVAL;
        return (-1);
    }
    e->ops.start (e->data);
    e->stats.starttime = get_timestamp ();
    e->stats.count = 0;
    e->stats.failcount = 0;
    e->stopped = 0;
    return (0);
}

static void cron_entry_destroy (cron_entry_t *e)
{
    struct cron_task *t;

    if (e == NULL)
        return;
    /*
     * Stop this entry first, then set a destroyed flag in the case we
     *  still have a task running
     */
    cron_entry_stop (e);
    e->destroyed = 1;

    /*
     *  If we have a task still running, we  have to leave cron_entry
     *   around until the task is complete.
     */
    if (e->task)
        return;

    /*
     *  Before destroying entry, remove it from entries list:
     */
    if (e->ctx && e->ctx->entries)
        zlist_remove (e->ctx->entries, e);

    if (e->data) {
        e->ops.destroy (e->data);
        e->data = NULL;
    }

    free (e->name);
    free (e->command);
    free (e->typename);
    free (e->cwd);

    if (e->env)
        json_decref (e->env);

    if (e->finished_tasks) {
        t = zlist_first (e->finished_tasks);
        while (t) {
            cron_task_destroy (t);
            t = zlist_next (e->finished_tasks);
        }
        zlist_destroy (&e->finished_tasks);
    }

    free (e);
}

static void deferred_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    cron_ctx_t *ctx = arg;
    cron_entry_t *e;
    while ((e = zlist_pop (ctx->deferred)))
        cron_entry_run_task (e);
    flux_msg_handler_stop (ctx->mh);
    ctx->last_sync = get_timestamp ();
}

static int cron_entry_defer (cron_entry_t *e)
{
    cron_ctx_t *ctx = e->ctx;
    double now = get_timestamp ();

    /* If no default synchronization event or the time since the last
     *  sync event is very short, then run task immediately
     */
    if (!ctx->mh || (now - ctx->last_sync) < ctx->sync_epsilon)
        return cron_entry_run_task (e);

    /* O/w, defer this task: push entry onto deferred list, and start
     *  sync event message handler if needed
     */
    if (zlist_push (ctx->deferred, e) < 0)
        return (-1);
    e->stats.deferred++;
    flux_log (ctx->h,
              LOG_DEBUG,
              "deferring cron-%ju to next %s event",
              e->id,
              ctx->sync_event);

    if (zlist_size (ctx->deferred) == 1)
        flux_msg_handler_start (ctx->mh);

    return (0);
}

static void cron_stats_init (struct cron_stats *s)
{
    memset (s, 0, sizeof (*s));
    s->ctime = get_timestamp ();
}

/*
 *  Create a new cron entry from JSON blob
 */
static cron_entry_t *cron_entry_create (cron_ctx_t *ctx, const flux_msg_t *msg)
{
    flux_t *h = ctx->h;
    cron_entry_t *e = NULL;
    json_t *args = NULL;
    const char *name;
    const char *command;
    const char *type;
    const char *cwd = NULL;
    int saved_errno = EPROTO;

    /* Get required fields "type", "name" and "command" */
    if (flux_msg_unpack (msg,
                         "{ s:s, s:s, s:s, s:O }",
                         "type", &type,
                         "name", &name,
                         "command", &command,
                         "args", &args) < 0) {
        flux_log_error (h, "cron.create: Failed to get name/command/args");
        goto done;
    }

    saved_errno = ENOMEM;
    if ((e = calloc (1, sizeof (*e))) == NULL) {
        flux_log_error (h, "cron.create: Out of memory");
        goto done;
    }

    e->id = ctx->next_id++;;
    e->ctx = ctx;
    e->stopped = 1;
    if (!(e->name = strdup (name)) || !(e->command = strdup (command))) {
        saved_errno = errno;
        goto out_err;
    }
    cron_stats_init (&e->stats);

    /*
     *  Set defaults for optional fields:
     */
    e->repeat = 0; /* Max number of times we'll run target command (0 = inf) */
    e->rank = 0;   /* Rank on which to run commands (default = 0)            */
    e->task_history_count = 1; /* Default number of entries in history list  */
    e->stop_on_failure = 0;    /* Whether the cron job is stopped on failure */
    e->timeout = -1.0;         /* Task timeout (default -1, no timeout)      */

    if (flux_msg_unpack (msg,
                         "{ s?O, s?s, s?i, s?i, s?i, s?i, s?F }",
                         "environ", &e->env,
                         "cwd", &cwd,
                         "repeat", &e->repeat,
                         "rank", &e->rank,
                         "task-history-count", &e->task_history_count,
                         "stop-on-failure", &e->stop_on_failure,
                         "timeout", &e->timeout) < 0) {
        saved_errno = EPROTO;
        flux_log_error (h, "cron.create: flux_msg_unpack");
        goto out_err;
    }

    if (!cwd)
        cwd = ctx->cwd;

    if ((e->cwd = strdup (cwd)) == NULL) {
        flux_log_error (h, "cron.create: strdup (cwd)");
        errno = ENOMEM;
        goto out_err;
    }

    /* List for all completed tasks up to task-history-count
     */
    if (!(e->finished_tasks = zlist_new ())) {
        saved_errno = errno;
        flux_log_error (h, "cron_entry_create: zlist_new");
        goto out_err;
    }

    /*
     *  Now, create type-specific data for this entry from type
     *   name and type-specific data in "args" key:
     */
    if (cron_type_operations_lookup (type, &e->ops) < 0) {
        saved_errno = ENOSYS; /* year,month,day,etc. not supported */
        goto out_err;
    }
    if ((e->typename = strdup (type)) == NULL) {
        saved_errno = errno;
        goto out_err;
    }
    if (!(e->data = e->ops.create (h, e, args))) {
        flux_log_error (h, "ops.create");
        saved_errno = errno;
        goto out_err;
    }
    json_decref (args);

    // Start the entry watcher for this type:
    cron_entry_start (e);

done:
    return (e);
out_err:
    cron_entry_destroy (e);
    errno = saved_errno;
    return (NULL);
}

static void cron_ctx_sync_event_stop (cron_ctx_t *ctx)
{
    if (ctx->sync_event) {
        if (flux_event_unsubscribe (ctx->h, ctx->sync_event) < 0)
            flux_log_error (ctx->h, "destroy: flux_event_unsubscribe\n");
        flux_msg_handler_destroy (ctx->mh);
        ctx->mh = NULL;
        free (ctx->sync_event);
        ctx->sync_event = NULL;
    }
}

static void cron_ctx_destroy (cron_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    cron_ctx_sync_event_stop (ctx);

    if (ctx->entries) {
        cron_entry_t *e;
        while ((e = zlist_pop (ctx->entries)))
            cron_entry_destroy (e);
        zlist_destroy (&ctx->entries);
    }
    if (ctx->deferred)
        zlist_destroy (&ctx->deferred);
    free (ctx->cwd);
    free (ctx);
}

static int cron_ctx_sync_event_init (cron_ctx_t *ctx, const char *topic)
{
    struct flux_match match = FLUX_MATCH_EVENT;

    flux_log (ctx->h,
              LOG_INFO,
              "synchronizing cron tasks to event %s",
              topic);

    if ((ctx->sync_event = strdup (topic)) == NULL) {
        flux_log_error (ctx->h, "sync_event_init: strdup");
        return (-1);
    }
    match.topic_glob = ctx->sync_event;
    ctx->mh = flux_msg_handler_create (ctx->h,
                                       match,
                                       deferred_cb,
                                       (void *) ctx);
    if (!ctx->mh) {
        flux_log_error (ctx->h, "sync_event_init: msg_handler_create");
        return (-1);
    }
    if (flux_event_subscribe (ctx->h, topic) < 0) {
        flux_log_error (ctx->h, "sync_event_init: subscribe (%s)", topic);
        return (-1);
    }
    /* Do not start the handler until we have entries on the list */
    return (0);
}

static cron_ctx_t * cron_ctx_create (flux_t *h)
{
    cron_ctx_t *ctx = calloc (1, sizeof (*ctx));
    if (ctx == NULL) {
        flux_log_error (h, "cron_ctx_create");
        goto error;
    }

    ctx->sync_event = NULL;
    ctx->last_sync  = 0.0;
    ctx->next_id    = 1;

    /* Default: run synchronized events up to 15ms after sync event */
    ctx->sync_epsilon = 0.015;
    ctx->mh = NULL;

    if (!(ctx->entries = zlist_new ())
        || !(ctx->deferred = zlist_new ())) {
        flux_log_error (h, "cron_ctx_create: zlist_new");
        goto error;
    }

    if (!(ctx->cwd = get_current_dir_name ())) {
        flux_log_error (h, "cron_ctx_create: get_get_current_dir_name");
        goto error;
    }

    ctx->h = h;
    return ctx;
error:
    cron_ctx_destroy (ctx);
    return (NULL);
}

/**************************************************************************/

static json_t *cron_stats_to_json (struct cron_stats *stats)
{
    return json_pack ("{ s:f, s:f, s:f, s:I, s:I, s:I, s:I, s:I, s:I }",
                      "ctime", stats->ctime,
                      "starttime", stats->starttime,
                      "lastrun", stats->lastrun,
                      "count", stats->count,
                      "failcount", stats->failcount,
                      "total", stats->total,
                      "success", stats->success,
                      "failure", stats->failure,
                      "deferred", stats->deferred);
}

static json_t *cron_entry_to_json (cron_entry_t *e)
{
    cron_task_t *t;
    json_t *o, *to;
    json_t *tasks;

    /*
     *  Common entry contents:
     */
    if (!(o = json_pack ("{ s:I, s:i, s:s, s:s, s:i, s:b, s:s }",
                         "id", (json_int_t) e->id,
                         "rank", e->rank,
                         "name", e->name,
                         "command", e->command,
                         "repeat", e->repeat,
                         "stopped", e->stopped,
                         "type", e->typename)))
        return NULL;

    if (e->timeout >= 0.0)
        json_object_set_new (o, "timeout", json_real (e->timeout));

    if ((to = cron_stats_to_json (&e->stats)))
        json_object_set_new (o, "stats", to);

    /*
     *  Add type specific json blob, under typedata key:
     */
    if ((to = e->ops.tojson (e->data)))
        json_object_set_new (o, "typedata", to);

    /*
     *  Add all task information, starting with any current task:
     */
    if (!(tasks = json_array ()))
        goto fail;
    if (e->task && (to = cron_task_to_json (e->task)))
        json_array_append_new (tasks, to);

    t = zlist_first (e->finished_tasks);
    while (t) {
        if ((to = cron_task_to_json (t)))
            json_array_append_new (tasks, to);
        t = zlist_next (e->finished_tasks);
    }

    json_object_set_new (o, "tasks", tasks);

    return (o);
fail:
    json_decref (o);
    return NULL;
}


/**************************************************************************/

/*
 *  Handle cron.create: create a new cron entry
 */
static void cron_create_handler (flux_t *h,
                                 flux_msg_handler_t *w,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_t *out = NULL;
    char *json_str = NULL;

    if (!(e = cron_entry_create (ctx, msg)))
        goto error;

    if (zlist_append (ctx->entries, e) < 0) {
        errno = ENOMEM;
        goto error;
    }

    if ((out = cron_entry_to_json (e))) {
        json_str = json_dumps (out, JSON_COMPACT);
        json_decref (out);
    }
    if (flux_respond (h, msg, json_str) < 0)
        flux_log_error (h, "cron.request: flux_respond");
    free (json_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.request: flux_respond_error");
}

static void cron_sync_handler (flux_t *h,
                               flux_msg_handler_t *w,
                               const flux_msg_t *msg,
                               void *arg)
{
    cron_ctx_t *ctx = arg;
    const char *topic;
    int disable;
    double epsilon;
    bool sync_event_before = ctx->sync_event ? true : false;

    if (flux_request_unpack (msg, NULL, "{}") < 0)
        goto error;
    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        topic = NULL; /* Disable sync-event */
    if (flux_request_unpack (msg, NULL, "{ s:b }", "disable", &disable) < 0)
        disable = false;

    if (topic || disable)
        cron_ctx_sync_event_stop (ctx);
    if (topic) {
        if (cron_ctx_sync_event_init (ctx, topic) < 0)
            goto error;
        /* If we changed the sync event, restart the message handler
         * if there are any current deferred jobs
         */
        if (zlist_size (ctx->deferred) > 0)
            flux_msg_handler_start (ctx->mh);
    }

    if (!flux_request_unpack (msg, NULL, "{ s:F }", "sync_epsilon", &epsilon))
        ctx->sync_epsilon = epsilon;

    if (ctx->sync_event) {
        if (flux_respond_pack (h,
                               msg,
                               "{ s:s s:f }",
                               "sync_event", ctx->sync_event,
                               "sync_epsilon", ctx->sync_epsilon) < 0)
            flux_log_error (h, "cron.request: flux_respond_pack");
    } else {
        if (sync_event_before) {
            /* If we just disabled a sync event, any cron jobs on the
             * deferred list can never be executed (i.e. the deferred
             * callback can never be triggered now).  These deferred
             * jobs would have already been executed if there wasn't a
             * sync event, so just execute them right now.
             */
            cron_entry_t *e;
            while ((e = zlist_pop (ctx->deferred)))
                cron_entry_run_task (e);
        }
        if (flux_respond_pack (h, msg, "{ s:b }", "sync_disabled", true) < 0)
            flux_log_error (h, "cron.request: flux_respond_pack");
    }
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.request: flux_respond_error");
}

static cron_entry_t *cron_ctx_find_entry (cron_ctx_t *ctx, int64_t id)
{
    cron_entry_t *e = zlist_first (ctx->entries);
    while (e && e->id != id)
        e = zlist_next (ctx->entries);
    return (e);
}

/*
 *  Return a cron entry referenced by request in flux message msg.
 *  [service] is name of service for logging purposes.
 */
static cron_entry_t *entry_from_request (flux_t *h,
                                         const flux_msg_t *msg,
                                         cron_ctx_t *ctx,
                                         const char *service)
{
    int64_t id;

    if (flux_request_unpack (msg, NULL, "{ s:I }", "id", &id) < 0) {
        flux_log_error (h, "%s: request decodef", service);
        return NULL;
    }

    errno = ENOENT;
    return cron_ctx_find_entry (ctx, id);
}

/*
 *  "cron.delete" handler
 */
static void cron_delete_handler (flux_t *h,
                                 flux_msg_handler_t *w,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_t *out = NULL;
    char *json_str = NULL;
    int kill = false;

    if (!(e = entry_from_request (h, msg, ctx, "cron.delete")))
        goto error;

    out = cron_entry_to_json (e);
    if (e->task
        && !flux_request_unpack (msg, NULL, "{ s:b }", "kill", &kill)
        && kill == true)
        cron_task_kill (e->task, SIGTERM);
    cron_entry_destroy (e);

    if (out)
        json_str = json_dumps (out, JSON_COMPACT);
    if (flux_respond (h, msg, json_str) < 0)
        flux_log_error (h, "cron.delete: flux_respond");
    free (json_str);
    json_decref (out);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.delete: flux_respond_error");
}

/*
 *  "cron.stop" handler: stop a cron entry until restarted
 */
static void cron_stop_handler (flux_t *h,
                               flux_msg_handler_t *w,
                               const flux_msg_t *msg,
                               void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_t *out = NULL;
    char *json_str = NULL;

    if (!(e = entry_from_request (h, msg, ctx, "cron.stop")))
        goto error;
    if (cron_entry_stop (e) < 0)
        goto error;
    if ((out = cron_entry_to_json (e))) {
        json_str = json_dumps (out, JSON_COMPACT);
        json_decref (out);
    }
    if (flux_respond (h, msg, json_str) < 0)
        flux_log_error (h, "cron.stop: flux_respond");
    free (json_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.stop: flux_respond_error");
}

/*
 *  "cron.start" handler: start a stopped cron entry
 */
static void cron_start_handler (flux_t *h,
                                flux_msg_handler_t *w,
                                const flux_msg_t *msg,
                                void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_t *out = NULL;
    char *json_str = NULL;

    if (!(e = entry_from_request (h, msg, ctx, "cron.start")))
        goto error;
    if (cron_entry_start (e) < 0)
        goto error;
    if ((out = cron_entry_to_json (e))) {
        json_str = json_dumps (out, JSON_COMPACT);
        json_decref (out);
    }
    if (flux_respond (h, msg, json_str) < 0)
        flux_log_error (h, "cron.start: flux_respond");
    free (json_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.start: flux_respond_error");
}


/*
 *  Handle "cron.list" -- dump a list of current cron entries via JSON
 */
static void cron_ls_handler (flux_t *h,
                             flux_msg_handler_t *w,
                             const flux_msg_t *msg,
                             void *arg)
{
    cron_ctx_t *ctx = arg;
    cron_entry_t *e = NULL;
    char *json_str = NULL;
    json_t *out = json_object ();
    json_t *entries = json_array ();

    if (out == NULL || entries == NULL) {
        flux_respond_error (h, msg, ENOMEM, NULL);
        flux_log_error (h, "cron.list: Out of memory");
        return;
    }

    e = zlist_first (ctx->entries);
    while (e) {
        json_t *entry = cron_entry_to_json (e);
        if (entry == NULL)
            flux_log_error (h, "cron_entry_to_json");
        else
            json_array_append_new (entries, entry);
        e = zlist_next (ctx->entries);
    }
    json_object_set_new (out, "entries", entries);

    if (!(json_str = json_dumps (out, JSON_COMPACT)))
        flux_log_error (h, "cron.list: json_dumps");
    else if (flux_respond (h, msg, json_str) < 0)
        flux_log_error (h, "cron.list: flux_respond");
    json_decref (out);
    free (json_str);
}

/**************************************************************************/

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,     "cron.create",   cron_create_handler, 0 },
    { FLUX_MSGTYPE_REQUEST,     "cron.delete",   cron_delete_handler, 0 },
    { FLUX_MSGTYPE_REQUEST,     "cron.list",     cron_ls_handler, 0 },
    { FLUX_MSGTYPE_REQUEST,     "cron.stop",     cron_stop_handler, 0 },
    { FLUX_MSGTYPE_REQUEST,     "cron.start",    cron_start_handler, 0 },
    { FLUX_MSGTYPE_REQUEST,     "cron.sync",     cron_sync_handler, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static int process_args (cron_ctx_t *ctx, int ac, char **av)
{
    int i;
    for (i = 0; i < ac; i++) {
        if (strstarts (av[i], "sync="))
            cron_ctx_sync_event_init (ctx, (av[i])+5);
        else if (strstarts (av[i], "sync_epsilon=")) {
            char *s = (av[i])+13;
            if (fsd_parse_duration (s, &ctx->sync_epsilon) < 0)
                flux_log_error (ctx->h, "option %s ignored", av[i]);
        }
        else {
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}


int mod_main (flux_t *h, int ac, char **av)
{
    int rc = -1;
    flux_msg_handler_t **handlers = NULL;
    cron_ctx_t *ctx = cron_ctx_create (h);
    if (ctx == NULL)
        return -1;

    if (process_args (ctx, ac, av) < 0)
        goto done;

    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");
done:
    flux_msg_handler_delvec (handlers);
    cron_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
