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

#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlogger.h"

struct shell_eventlogger {
    flux_shell_t *shell;
    zhashx_t *contexts;
    struct eventlogger *ev;
};

static void json_free (void **item)
{
    if (item) {
        json_t *o = *item;
        json_decref (o);
        *item = NULL;
    }
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

void shell_eventlogger_destroy (struct shell_eventlogger *shev)
{
    if (shev) {
        zhashx_destroy (&shev->contexts);
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
        || !(shev->contexts = zhashx_new ())) {
        shell_eventlogger_destroy (shev);
        return NULL;
    }
    shev->shell = shell;
    zhashx_set_destructor (shev->contexts, json_free);
    return shev;
}

int shell_eventlogger_emit_event (struct shell_eventlogger *shev,
                                  int flags,
                                  const char *event)
{
    int rc;
    char *context = NULL;
    json_t *o = zhashx_lookup (shev->contexts, event);
    if (o != NULL)
        context = json_dumps (o, JSON_COMPACT);
    rc = eventlogger_append (shev->ev,
                             EVENTLOGGER_FLAG_WAIT,
                             "exec.eventlog",
                             event,
                             context);
    free (context);
    return rc;
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
