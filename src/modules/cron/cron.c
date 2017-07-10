/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "task.h"
#include "entry.h"
#include "types.h"

struct cron_ctx {
    flux_t *               h;
    char *                 sync_event;      /* If set, sync entries to event */
    flux_msg_handler_t *   mh;              /* sync event message handler    */
    zlist_t    *           entries;
    zlist_t    *           deferred;        /* list of deferred entries      */
    double                 last_sync;       /* timestamp of last sync event  */
    double                 sync_epsilon;    /* allow tasks to run for this
                                               number of seconds after last-
                                               sync before deferring         */
};

/**************************************************************************
 * Forward declarations
 */
static cron_entry_t *cron_entry_create (cron_ctx_t *ctx,
    const char *json);
static void cron_entry_destroy (cron_entry_t *e);
static int cron_entry_stop (cron_entry_t *e);
static void cron_entry_completion_handler (flux_t *h, cron_task_t *t,
    void *arg);
static void cron_entry_io_cb (flux_t *h, cron_task_t *t, void *arg,
    bool is_stderr, void *data, int datalen, bool eof);
static int cron_entry_run_task (cron_entry_t *e);
static int cron_entry_defer (cron_entry_t *e);

/**************************************************************************/
/* Public apis
 */
void *cron_entry_type_data (cron_entry_t *e)
{
    return e->data;
}

static double timespec_to_double (struct timespec *tm)
{
    double s = tm->tv_sec;
    double ns = tm->tv_nsec/1.0e9 + .5e-9; // round 1/2 epsilon
    return (s + ns);
}

double get_timestamp (void)
{
    struct timespec tm;
    clock_gettime (CLOCK_REALTIME, &tm);
    return timespec_to_double (&tm);
}

static void timeout_cb (flux_t *h, cron_task_t *t, void *arg)
{
    cron_entry_t *e = arg;
    flux_log (h, LOG_INFO, "cron-%ju: task timeout at %.2fs. Killing",
              e->id, e->timeout);
    cron_task_kill (t, SIGTERM);
}

static int cron_entry_run_task (cron_entry_t *e)
{
    flux_t *h = e->ctx->h;
    if (cron_task_run (e->task, e->rank, e->command, NULL, NULL) < 0) {
        flux_log_error (h, "cron-%ju: cron_task_run", e->id);
        /* Run "completion" handler since this task is done */
        cron_entry_completion_handler (h, e->task, e);
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
        flux_log (h, LOG_INFO, "cron-%ju: %s: task still running or scheduled",
                  e->id, e->name);
        return (0);
    }
    if (!(e->task = cron_task_new (h, cron_entry_completion_handler, e))) {
        flux_log_error (h, "cron_task_create");
        return -1;
    }
    cron_task_on_io (e->task, cron_entry_io_cb);
    if (e->timeout >= 0.0)
        cron_task_set_timeout (e->task, e->timeout, timeout_cb);

    /*   if we've reached our (non-zero) repeat count, prematurely stop
     *     the current entry (i.e. remove it from event loop, but leave
     *     it in ctx->entries so it can be listed/queried)
     */
    if (cron_entry_increment (e) == e->repeat)
        cron_entry_stop (e);

    return cron_entry_defer (e);
}


/**************************************************************************/

static void cron_entry_loglines (flux_t *h, cron_entry_t *e, int level, char *s)
{
    char *p, *saveptr = NULL;
    while ((p = strtok_r (s, "\n", &saveptr))) {
        flux_log (h, level, "cron-%ju[%s]: rank=%d: command=\"%s\": \"%s\"",
                e->id, e->name, e->rank, e->command, p);
        s = NULL;
    }
}

static void cron_entry_io_cb (flux_t *h, cron_task_t *t, void *arg,
    bool is_stderr, void *data, int datalen, bool eof)
{
    if (data)
        cron_entry_loglines (h, arg, is_stderr ? LOG_ERR : LOG_INFO, data);
}

/* Push task t onto the front of the completed tasks list for
 *  entry e. If the list has grown past completed-task-size, then drop the
 *  tail task on the list.
 */
int cron_entry_push_completed_task (cron_entry_t *e, struct cron_task *t)
{
    if (zlist_push (e->completed_tasks, t) < 0)
        return (-1);
    if (zlist_size (e->completed_tasks) > e->task_history_count) {
        struct cron_task *tdel = zlist_tail (e->completed_tasks);
        if (tdel) {
            zlist_remove (e->completed_tasks, tdel);
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
        flux_log (e->ctx->h, LOG_ERR,
                "cron-%ju: exceeded failure limit of %d. stopping",
                e->id, e->stop_on_failure);
        cron_entry_stop (e);
    }
}

static void cron_entry_completion_handler (flux_t *h, cron_task_t *t, void *arg)
{
    cron_entry_t *e = arg;

    if (strcmp (cron_task_state (t), "Exec Failure") == 0) {
        flux_log_error (h, "cron-%ju: failed to run %s", e->id, e->command);
        cron_entry_failure (e);
    }
    else if (cron_task_status (t) != 0) {
        flux_log (h, LOG_ERR, "cron-%ju: \"%s\": Failed: %s",
                 e->id, e->command, cron_task_state (t));
        cron_entry_failure (e);
    }
    else
        e->stats.success++;

    /*
     *  Push the completed task onto the completed task list and
     *   drop a task if needed. Reset e->task to NULL since there
     *   is currently no active task.
     */
    if (cron_entry_push_completed_task (e, t) < 0)
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
static void entry_stop_cb (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    cron_entry_stop (arg);
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
}

/* Stop cron entry `e` "safely" by waiting until the next
 *  "prepare" callback. Temporary watcher created here wil lbe
 *  destroyed within prepare_cb.
 */
int cron_entry_stop_safe (cron_entry_t *e)
{
    flux_reactor_t *r = flux_get_reactor (e->ctx->h);
    flux_watcher_t *w = flux_prepare_watcher_create (r,
                            entry_stop_cb, e);
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
    zlist_remove (e->ctx->entries, e);

    if (e->data) {
        e->ops.destroy (e->data);
        e->data = NULL;
    }

    free (e->name);
    free (e->command);

    t = zlist_first (e->completed_tasks);
    while (t) {
        cron_task_destroy (t);
        t = zlist_next (e->completed_tasks);
    }
    zlist_destroy (&e->completed_tasks);

    free (e);
}

/*
 *  Get the next ID from global "cron" sequence number.
 *  This may be overkill, but is simplest so we go ahead an do it.
 */
static int64_t next_cronid (flux_t *h)
{
    int64_t ret = (int64_t) -1;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h, "seq.fetch", 0, 0,
                             "{ s:s s:i s:i s:b }",
                             "name", "cron",
                             "preincrement", 1,
                             "postincrement", 0,
                             "create", true))) {
        flux_log_error (h, "flux_rpc_pack");
        goto out;
    }

    if (flux_rpc_get_unpack (f, "{ s:I }", "value", &ret) < 0) {
        flux_log_error (h, "next_cronid: rpc_get_unpack");
        goto out;
    }

out:
    flux_future_destroy (f);
    return ret;
}

static void deferred_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
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
    flux_log (ctx->h, LOG_DEBUG, "deferring cron-%ju to next %s event",
             e->id, ctx->sync_event);

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
static cron_entry_t *cron_entry_create (cron_ctx_t *ctx, const char *json)
{
    flux_t *h = ctx->h;
    cron_entry_t *e = NULL;
    json_object *in;
    const char *name;
    const char *command;
    const char *type;
    int saved_errno = EPROTO;

    if (!(in = Jfromstr (json))) {
        flux_log_error (h, "cron request decode");
        goto done;
    }

    /* Get required fields "name" and "command" */
    if (!Jget_str (in, "name", &name) ||
        !Jget_str (in, "command", &command)) {
        flux_log_error (h, "cron.create: Failed to get name/command");
        goto done;
    }

    e = xzmalloc (sizeof (*e));
    memset (e, 0, sizeof (*e));

    /* Allocate a new ID from this cron entry:
     */
    if ((e->id = next_cronid (h)) < 0) {
        cron_entry_destroy (e);
        e = NULL;
        saved_errno = errno;
        goto done;
    }

    e->ctx = ctx;
    e->stopped = 1;
    e->name = xstrdup (name);
    e->command = xstrdup (command);
    cron_stats_init (&e->stats);

    /* Get optional fields
     *  "repeat" -- the max number of times we'll run the target command
     *  "rank"   -- run target command on this rank (default 0)
     */
    (void) Jget_int (in, "repeat", &e->repeat);
    (void) Jget_int (in, "rank", &e->rank);

    /* Check to see if we want to keep more than just the status of
     *  previous run for this cron entry.
     */
    if (!Jget_int (in, "task-history-count", &e->task_history_count))
        e->task_history_count = 1;

    if (!Jget_int (in, "stop-on-failure", &e->stop_on_failure))
        e->stop_on_failure = 0;

    if (!Jget_double (in, "timeout", &e->timeout))
        e->timeout = -1.0;

    /* List for all completed tasks up to task-history-count
     */
    if (!(e->completed_tasks = zlist_new ())) {
        saved_errno = errno;
        flux_log_error (h, "cron_entry_create: zlist_new");
        goto out_err;
    }

    /*
     *  Now, create type-specific data for this entry from type
     *   name and type-specific data in "args" key:
     */
    if (!Jget_str (in, "type", &type)
        || (cron_type_operations_lookup (type, &e->ops) < 0)) {
        saved_errno = ENOSYS; /* year,month,day,etc. not supported */
        goto out_err;
    }
    e->typename = xstrdup (type);

    if (!(e->data = e->ops.create (h, e, Jobj_get (in, "args")))) {
        saved_errno = errno;
        goto out_err;
    }

    // Start the entry watcher for this type:
    cron_entry_start (e);

done:
    if (in)
        Jput (in);
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
    free (ctx);
}

static int cron_ctx_sync_event_init (cron_ctx_t *ctx, const char *topic)
{
    struct flux_match match = FLUX_MATCH_EVENT;

    flux_log (ctx->h, LOG_INFO,
        "synchronizing cron tasks to event %s", topic);

    ctx->sync_event = xstrdup (topic);
    match.topic_glob = ctx->sync_event;
    ctx->mh = flux_msg_handler_create (ctx->h, match,
                                       deferred_cb, (void *) ctx);
    if (!ctx->mh) {
        flux_log_error (ctx->h, "sync_event_init: msg_handler_create");
        return (-1);
    }
    if (flux_event_subscribe (ctx->h, topic) < 0) {
        flux_log_error (ctx->h, "sync_event_init: subscribe (%s)", topic);
        return (-1);
    }
    /* Do not start the handler until we have entries on the the list */
    return (0);
}

static cron_ctx_t * cron_ctx_create (flux_t *h)
{
    cron_ctx_t *ctx = xzmalloc (sizeof (*ctx));

    ctx->sync_event = NULL;
    ctx->last_sync  = 0.0;

    /* Default: run synchronized events up to 15ms after sync event */
    ctx->sync_epsilon = 0.015;
    ctx->mh = NULL;

    if (!(ctx->entries = zlist_new ())
        || !(ctx->deferred = zlist_new ())) {
        flux_log_error (h, "cron_ctx_create: zlist_new");
        goto error;
    }
    ctx->h = h;
    return ctx;
error:
    cron_ctx_destroy (ctx);
    return (NULL);
}

/**************************************************************************/

static json_object *cron_stats_to_json (struct cron_stats *stats)
{
    json_object *o = Jnew ();
    Jadd_double (o, "ctime", stats->ctime);
    Jadd_double (o, "starttime", stats->starttime);
    Jadd_double (o, "lastrun", stats->lastrun);
    Jadd_int (o, "count", stats->count);
    Jadd_int (o, "failcount", stats->failcount);
    Jadd_int (o, "total", stats->total);
    Jadd_int (o, "success", stats->success);
    Jadd_int (o, "failure", stats->failure);
    Jadd_int (o, "deferred", stats->deferred);
    return (o);
}

static json_object *cron_entry_to_json (cron_entry_t *e)
{
    cron_task_t *t;
    json_object *o = Jnew ();
    json_object *to = NULL;
    json_object *tasks = Jnew_ar ();

    /*
     *  Common entry contents:
     */
    Jadd_int64 (o, "id", e->id);
    Jadd_int   (o, "rank", e->rank);
    Jadd_str   (o, "name", e->name);
    Jadd_str   (o, "command", e->command);
    Jadd_int   (o, "repeat", e->repeat);
    Jadd_bool  (o, "stopped", e->stopped);
    Jadd_str   (o, "type", e->typename);
    if (e->timeout >= 0.0)
        Jadd_double (o, "timeout", e->timeout);

    if ((to = cron_stats_to_json (&e->stats)))
        json_object_object_add (o, "stats", to);

    /*
     *  Add type specific json blob, under typedata key:
     */
    if ((to = e->ops.tojson (e->data)))
        json_object_object_add (o, "typedata", to);

    /*
     *  Add all task information, starting with any current task:
     */
    if (e->task && (to = cron_task_to_json (e->task)))
        json_object_array_add (tasks, to);

    t = zlist_first (e->completed_tasks);
    while (t) {
        if ((to = cron_task_to_json (t)))
            json_object_array_add (tasks, to);
        t = zlist_next (e->completed_tasks);
    }

    json_object_object_add (o, "tasks", tasks);

    return (o);
}


/**************************************************************************/

/*
 *  Handle cron.create: create a new cron entry
 */
static void cron_create_handler (flux_t *h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    const char *json_str;
    json_object *out = NULL;
    int saved_errno = EPROTO;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (h, "cron request decode");
        saved_errno = errno;
        goto done;
    }

    if (!json_str) {
        saved_errno = EPROTO;
        goto done;
    }

    if (!(e = cron_entry_create (ctx, json_str))) {
        saved_errno = errno;
        goto done;
    }

    if (zlist_append (ctx->entries, e) < 0) {
        saved_errno = errno;
        goto done;
    }

    rc = 0;
    out = cron_entry_to_json (e);
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "cron.request: flux_respond");
    Jput (out);
}

static void cron_sync_handler (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    cron_ctx_t *ctx = arg;
    const char *topic;
    int disable;
    double epsilon;
    int rc = -1;

    if (flux_request_unpack (msg, NULL, "{}") < 0)
        goto error;
    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        topic = NULL; /* Disable sync-event */
    if (flux_request_unpack (msg, NULL, "{ s:b }", "disable", &disable) < 0)
        disable = false;

    if (topic || disable)
        cron_ctx_sync_event_stop (ctx);
    rc = topic ? cron_ctx_sync_event_init (ctx, topic) : 0;
    if (rc < 0)
        goto error;

    if (!flux_request_unpack (msg, NULL, "{ s:F }", "sync_epsilon", &epsilon))
        ctx->sync_epsilon = epsilon;

    if (ctx->sync_event) {
        if (flux_respondf (h, msg, "{ s:s s:f }",
                           "sync_event", ctx->sync_event,
                           "sync_epsilon", ctx->sync_epsilon) < 0)
            flux_log_error (h, "cron.request: flux_respondf");
    } else {
        if (flux_respondf (h, msg, "{ s:b }", "sync_disabled", true) < 0)
            flux_log_error (h, "cron.request: flux_respondf");
    }
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "cron.request: flux_respond");
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
static cron_entry_t *entry_from_request (flux_t *h, const flux_msg_t *msg,
                                         cron_ctx_t *ctx, const char *service)
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
static void cron_delete_handler (flux_t *h, flux_msg_handler_t *w,
                                 const flux_msg_t *msg, void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_object *out = NULL;
    int kill = false;
    int saved_errno;
    int rc = -1;

    if (!(e = entry_from_request (h, msg, ctx, "cron.delete"))) {
        saved_errno = errno;
        goto done;
    }

    rc = 0;
    out = cron_entry_to_json (e);
    if (e->task
        && !flux_request_unpack (msg, NULL, "{ s:b }", "kill", &kill)
        && kill == true)
        cron_task_kill (e->task, SIGTERM);
    cron_entry_destroy (e);

done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "cron.delete: flux_respond");
    Jput (out);
}

/*
 *  "cron.stop" handler: stop a cron entry until restarted
 */
static void cron_stop_handler (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_object *out = NULL;
    int saved_errno = 0;
    int rc = -1;

    if (!(e = entry_from_request (h, msg, ctx, "cron.stop"))) {
        saved_errno = errno;
        goto done;
    }
    rc = cron_entry_stop (e);
    out = cron_entry_to_json (e);
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              out ? Jtostr (out) : NULL) < 0)
        flux_log_error (h, "cron.stop: flux_respond");
}

/*
 *  "cron.start" handler: start a stopped cron entry
 */
static void cron_start_handler (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    cron_entry_t *e;
    cron_ctx_t *ctx = arg;
    json_object *out = NULL;
    int saved_errno = 0;
    int rc = -1;

    if (!(e = entry_from_request (h, msg, ctx, "cron.start"))) {
        saved_errno = errno;
        goto done;
    }
    rc = cron_entry_start (e);
    out = cron_entry_to_json (e);
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                      out ? Jtostr (out) : NULL) < 0)
        flux_log_error (h, "cron.start: flux_respond");
}


/*
 *  Handle "cron.list" -- dump a list of current cron entries via JSON
 */
static void cron_ls_handler (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    cron_ctx_t *ctx = arg;
    cron_entry_t *e = NULL;
    json_object *out = Jnew ();
    json_object *entries = Jnew_ar ();

    e = zlist_first (ctx->entries);
    while (e) {
        json_object *entry = cron_entry_to_json (e);
        if (entry == NULL)
            flux_log_error (h, "cron_entry_to_json");
        else
            json_object_array_add (entries, entry);
        e = zlist_next (ctx->entries);
    }
    json_object_object_add (out, "entries", entries);

    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        flux_log_error (h, "cron.list: flux_respond");
    Jput (out);
}

/**************************************************************************/

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,     "cron.create",   cron_create_handler, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,     "cron.delete",   cron_delete_handler, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,     "cron.list",     cron_ls_handler, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,     "cron.stop",     cron_stop_handler, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,     "cron.start",    cron_start_handler, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,     "cron.sync",     cron_sync_handler, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

static void process_args (cron_ctx_t *ctx, int ac, char **av)
{
    int i;
    for (i = 0; i < ac; i++) {
        if (strncmp (av[i], "sync=", 5) == 0)
            cron_ctx_sync_event_init (ctx, (av[i])+5);
        else if (strncmp (av[i], "sync_epsilon=", 13) == 0) {
            char *s = (av[i])+13;
            char *endptr;
            double d = strtod (s, &endptr);
            if (d == 0 && endptr == s)
                flux_log_error (ctx->h, "option %s ignored", av[i]);
            else
                ctx->sync_epsilon = d;
        }
        else
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
    }
}


int mod_main (flux_t *h, int ac, char **av)
{
    int rc = -1;
    cron_ctx_t *ctx = cron_ctx_create (h);

    process_args (ctx, ac, av);

    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");
    flux_msg_handler_delvec (htab);
    cron_ctx_destroy (ctx);
done:
    return rc;
}

MOD_NAME ("cron");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
