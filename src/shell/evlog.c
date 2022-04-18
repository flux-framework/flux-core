/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  evlog - Write shell log messages to output eventlog as RFC 24
 *   Log Events.
 *
 *  Log messages are forwarded to this plugin from the shell log
 *   facility via a "shell.log" plugin hook, which provides plugin
 *   argument in RFC 24 Log Event format. If the severity level of
 *   the message is equal to or lower than the current set level,
 *   then the message is appended to the output eventlog using
 *   an "eventlogger" abstraction, which batches events when
 *   possible.
 *
 *  Log messages at FLUX_SHELL_FATAL are never batched and instead
 *   are written synchronously to the event log (i.e. the plugin
 *   hook will not return until the kvs commit has posted).
 *
 *  The evlog plugin also subscribes to the "shell.log-setlevel"
 *   plugin hook, which allows the level of one or more logging
 *   plugins to be set independently of the main shell log
 *   facility level.
 */
#define FLUX_SHELL_PLUGIN_NAME "evlog"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libeventlog/eventlogger.h"

#include "info.h"
#include "internal.h"
#include "builtins.h"

struct evlog {
    int sync_mode;
    int level;
    flux_shell_t *shell;
    struct eventlogger *ev;
};

static int log_eventlog (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    int flags = 0;
    int rc = 0;
    int level = -1;
    char *context = NULL;
    struct evlog *evlog = NULL;

    if (!(evlog = flux_plugin_aux_get (p, "evlog")))
        return -1;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:i}", "level", &level) < 0)
        return -1;
    if (level > evlog->level)
        return 0;
    if (evlog->sync_mode || level == FLUX_SHELL_FATAL)
        flags = EVENTLOGGER_FLAG_WAIT;
    if (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_IN, &context) < 0
        || eventlogger_append (evlog->ev, flags, "output", "log", context) < 0)
        rc = -1;
    free (context);
    return rc;
}

static void evlog_destroy (struct evlog *evlog)
{
    /*  Redirect future logging to stderr */
    flux_shell_log_setlevel (evlog->level, "stderr");

    eventlogger_flush (evlog->ev);
    eventlogger_destroy (evlog->ev);
    free (evlog);
}

static void evlog_ref (struct eventlogger *ev, void *arg)
{
    struct evlog *evlog = arg;
    flux_shell_add_completion_ref (evlog->shell, "eventlogger.txn");
}

static void evlog_unref (struct eventlogger *ev, void *arg)
{
    struct evlog *evlog = arg;
    flux_shell_remove_completion_ref (evlog->shell, "eventlogger.txn");
}

static int log_eventlog_reconnect (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);

    /* during a reconnect, response to event logging may not occur,
     * thus evlog_unref() may not be called.  Clear all completion
     * references to inflight transactions.
     */

    while (flux_shell_remove_completion_ref (shell, "eventlogger.txn") == 0);

    return 0;
}

static void evlog_error (struct eventlogger *ev,
                         void *arg,
                         int errnum,
                         json_t *entry)
{
    const char *msg;
    if (json_unpack (entry, "{s:{s:s}}",
                            "context",
                            "message", &msg) < 0) {
        fprintf (stderr, "evlog_error: failed to unpack message\n");
        return;
    }
    fprintf (stderr, "evlog: %s: msg=%s\n",
                     strerror (errnum), msg);
}

static struct evlog *evlog_create (flux_shell_t *shell)
{
    flux_t *h = flux_shell_get_flux (shell);
    struct eventlogger_ops ops = {
        .busy = evlog_ref,
        .idle = evlog_unref,
        .err =  evlog_error
    };
    struct evlog *evlog = calloc (1, sizeof (*evlog));

    if (h == NULL) {
        fprintf (stderr, "evlog_create failure due to no handle\n");
        return NULL;
    }
    if (!evlog || !(evlog->ev = eventlogger_create (h, 0.01, &ops, evlog)))
        goto err;
    eventlogger_set_commit_timeout (evlog->ev, 5.);
    evlog->level = FLUX_SHELL_NOTICE + shell->verbose;
    evlog->shell = shell;
    return evlog;
err:
    evlog_destroy (evlog);
    return NULL;
}

/*  Check if a shell.log-setlevel request is for dest="any" or "eventlog"
 *   and adjust our internal level if so
 */
static int log_eventlog_setlevel (flux_plugin_t *p,
                                  const char *topic,
                                  flux_plugin_arg_t *args,
                                  void *data)
{
    struct evlog *evlog = data;
    const char *name;
    int level;

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{ s:s s:i }",
                                "dest", &name,
                                "level", &level) < 0) {
        fprintf (stderr, "log.eventlog: setlevel arg unpack error: %s\n",
                         flux_plugin_arg_strerror (args));
        return -1;
    }
    if (strcmp (name, "any") == 0 || strcmp (name, "eventlog") == 0)
        evlog->level = level;
    return 0;
}

static int evlog_shell_exit (flux_plugin_t *p,
                             const char *topic,
                             flux_plugin_arg_t *args,
                             void *data)
{
    struct evlog *evlog = data;
    /*
     *  Write all log messages synchronously after shell.exit, since
     *   there will no longer be a reactor.
     */
    evlog->sync_mode = 1;
    return 0;
}

/*  Start the eventlog-based logger during shell.connect, just after the
 *   shell has obtained a flux_t handle. This allows more early log
 *   messages to make it into the eventlog, but some data (such as
 *   the current shell_rank) is not available at this time.
 */
static int log_eventlog_start (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct evlog *evlog = NULL;

    /*  Do not activate eventlogger in standlone mode */
    if (shell->standalone)
        return 0;

    if (!(evlog = evlog_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p, "evlog", evlog,
                             (flux_free_f) evlog_destroy ) < 0)
        goto err;
    if (flux_plugin_add_handler (p, "shell.log", log_eventlog, shell) < 0
       || flux_plugin_add_handler (p, "shell.log-setlevel",
                                   log_eventlog_setlevel,
                                   evlog) < 0
       || flux_plugin_add_handler (p, "shell.exit",
                                   evlog_shell_exit,
                                   evlog) < 0)
        goto err;

    /*  Disable stderr logging */
    flux_shell_log_setlevel (FLUX_SHELL_QUIET, "stderr");
    return 0;
err:
    evlog_destroy (evlog);
    return -1;
}

struct shell_builtin builtin_log_eventlog = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .connect = log_eventlog_start,
    .reconnect = log_eventlog_reconnect,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
