/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Shell exec.eventlog event emitter
 *  Allows context for shell events to be added from multiple sources.
 */
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <assert.h>

#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/eventlogger.h"

#include "events.h"

struct shell_eventlogger {
    flux_shell_t *shell;
    zhashx_t *contexts;
    zlistx_t *emitted_events;
    struct eventlogger *ev;
};

struct emitted_event {
    char *event;
    void *handle;
    bool confirmed_logged;
};

static void json_free (void **item)
{
    if (item) {
        json_t *o = *item;
        json_decref (o);
        *item = NULL;
    }
}

static void emitted_event_destroy (void *data)
{
    if (data) {
        struct emitted_event *e = data;
        free (e->event);
        free (e);
    }
}

static void emitted_event_destroy_wrapper (void **data)
{
    if (data) {
        emitted_event_destroy (*data);
        *data = NULL;
    }
}

static struct emitted_event *emitted_event_create (const char *event)
{
    struct emitted_event *e = calloc (1, sizeof (*e));
    if (!e)
        return NULL;
    if (!(e->event = strdup (event)))
        goto error;
    return e;

error:
    emitted_event_destroy (e);
    return NULL;
}

static int emitted_event_append (struct shell_eventlogger *shev, const char *event)
{
    struct emitted_event *e = emitted_event_create (event);
    if (!e)
        return -1;
    if (!(e->handle = zlistx_add_end (shev->emitted_events, e))) {
        emitted_event_destroy (e);
        return -1;
    }
    return 0;
}

static void shell_eventlogger_ref (struct eventlogger *ev, void *arg)
{
    struct shell_eventlogger *shev = arg;
    flux_shell_add_completion_ref (shev->shell, "shell_eventlogger");
}

static void shell_eventlogger_unref (struct eventlogger *ev, void *arg)
{
    struct shell_eventlogger *shev = arg;
    flux_shell_remove_completion_ref (shev->shell, "shell_eventlogger");
}

static int emit_event (struct shell_eventlogger *shev,
                       const char *event,
                       bool save_to_emitted_events)
{
    int rc = -1;
    char *context = NULL;
    json_t *o = zhashx_lookup (shev->contexts, event);
    if (o != NULL)
        context = json_dumps (o, JSON_COMPACT);
    if (eventlogger_append (shev->ev,
                            EVENTLOGGER_FLAG_WAIT,
                            "exec.eventlog",
                            event,
                            context) < 0)
        goto error;
    if (save_to_emitted_events) {
        if (emitted_event_append (shev, event) < 0)
            goto error;
    }
    rc = 0;
error:
    free (context);
    return rc;
}

static int shell_eventlogger_compare_eventlog (struct shell_eventlogger *shev)
{
    flux_future_t *f = NULL;
    struct emitted_event *e;
    const char *s = NULL;
    int rv = -1;

    if (!(f = flux_kvs_lookup (flux_shell_get_flux (shev->shell),
                               NULL,
                               0,
                               "exec.eventlog")))
        return -1;

    /* do this synchronously, since we are reconnecting */
    if (flux_kvs_lookup_get (f, &s) < 0)
        goto error;

    e = zlistx_first (shev->emitted_events);
    while (e) {
        if (!e->confirmed_logged) {
            int ret;
            if ((ret = eventlog_contains_event (s, e->event)) < 0)
                goto error;
            if (ret == 1) {
                if (emit_event (shev, e->event, false) < 0)
                    goto error;
                e->confirmed_logged = true;
            }
        }
        e = zlistx_next (shev->emitted_events);
    }

    rv = 0;
error:
    flux_future_destroy (f);
    return rv;
}

int shell_eventlogger_reconnect (struct shell_eventlogger *shev)
{
    /* during a reconnect, response to event logging may not occur,
     * thus shell_eventlogger_unref() may not be called.  Clear all
     * completion references to inflight transactions.
     */

    while (flux_shell_remove_completion_ref (shev->shell,
                                             "shell_eventlogger") == 0);

    /* exec.eventlog events are often critical to correct function, so
     * if any were lost during a reconnect, we need to make sure it
     * was logged, otherwise try to write it out again.
     */
    if (shell_eventlogger_compare_eventlog (shev) < 0)
        return -1;

    return 0;
}

void shell_eventlogger_destroy (struct shell_eventlogger *shev)
{
    if (shev) {
        zhashx_destroy (&shev->contexts);
        zlistx_destroy (&shev->emitted_events);
        eventlogger_destroy (shev->ev);
        free (shev);
    }
}

struct shell_eventlogger *shell_eventlogger_create (flux_shell_t *shell)
{
    flux_t *h;
    struct eventlogger_ops ops = {
        .busy = shell_eventlogger_ref,
        .idle = shell_eventlogger_unref
    };
    struct shell_eventlogger *shev = calloc (1, sizeof (*shev));

    if (!(h = flux_shell_get_flux (shell))
        || !(shev->ev = eventlogger_create (h, 0.01, &ops, shev))
        || !(shev->contexts = zhashx_new ())
        || !(shev->emitted_events = zlistx_new ())) {
        shell_eventlogger_destroy (shev);
        return NULL;
    }
    shev->shell = shell;
    zhashx_set_destructor (shev->contexts, json_free);
    zlistx_set_destructor (shev->emitted_events, emitted_event_destroy_wrapper);
    return shev;
}

int shell_eventlogger_emit_event (struct shell_eventlogger *shev,
                                  const char *event)
{
    return emit_event (shev, event, true);
}

static int context_set (struct shell_eventlogger *shev,
                        const char *name,
                        int flags,
                        json_t *o)
{
    json_t *orig = zhashx_lookup (shev->contexts, name);
    if (orig != NULL) {
        int rc = json_object_update (orig, o);
        /*  mem for o now "owned" by orig */
        json_decref (o);
        return rc;
    }
    return zhashx_insert (shev->contexts, name, o);
}

int shell_eventlogger_context_vpack (struct shell_eventlogger *shev,
                                     const char *event,
                                     int flags,
                                     const char *fmt,
                                     va_list ap)
{
    json_t *o;
    json_error_t err;
    if (!shev || !fmt || !event) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = json_vpack_ex (&err, 0, fmt, ap)))
        return -1;
    return context_set (shev, event, flags, o);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
