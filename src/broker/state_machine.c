/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"

#include "state_machine.h"

#include "broker.h"
#include "shutdown.h"
#include "runat.h"
#include "overlay.h"

struct monitor {
    zlist_t *requests;

    flux_future_t *f;
    broker_state_t parent_state;
    unsigned int parent_valid:1;
    unsigned int parent_error:1;
};

struct state_machine {
    struct broker *ctx;
    broker_state_t state;

    zlist_t *events;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;

    flux_msg_handler_t **handlers;

    struct monitor monitor;
};

typedef void (*action_f)(struct state_machine *s);

struct state {
    broker_state_t state;
    const char *name;
    action_f action;
};

struct state_next {
    const char *event;
    broker_state_t current;
    broker_state_t next;
};

static void action_join (struct state_machine *s);
static void action_init (struct state_machine *s);
static void action_run (struct state_machine *s);
static void action_cleanup (struct state_machine *s);
static void action_finalize (struct state_machine *s);
static void action_shutdown (struct state_machine *s);

static void runat_completion_cb (struct runat *r, const char *name, void *arg);
static void monitor_update (flux_t *h, zlist_t *requests, broker_state_t state);
static void join_check_parent (struct state_machine *s);

static struct state statetab[] = {
    { STATE_NONE,       "none",             NULL },
    { STATE_JOIN,       "join",             action_join },
    { STATE_INIT,       "init",             action_init },
    { STATE_RUN,        "run",              action_run },
    { STATE_CLEANUP,    "cleanup",          action_cleanup },
    { STATE_FINALIZE,   "finalize",         action_finalize },
    { STATE_SHUTDOWN,   "shutdown",         action_shutdown },
};

static struct state_next nexttab[] = {
    { "wireup-complete",    STATE_NONE,         STATE_JOIN },
    { "parent-ready",       STATE_JOIN,         STATE_INIT },
    { "parent-none",        STATE_JOIN,         STATE_INIT },
    { "parent-fail",        STATE_JOIN,         STATE_SHUTDOWN },
    { "rc1-success",        STATE_INIT,         STATE_RUN },
    { "rc1-none",           STATE_INIT,         STATE_RUN },
    { "rc1-fail",           STATE_INIT,         STATE_FINALIZE },
    { "rc2-success",        STATE_RUN,          STATE_CLEANUP },
    { "rc2-fail",           STATE_RUN,          STATE_CLEANUP },
    { "rc2-abort",          STATE_RUN,          STATE_CLEANUP },
    { "rc2-none",           STATE_RUN,          STATE_RUN },
    { "cleanup-success",    STATE_CLEANUP,      STATE_FINALIZE},
    { "cleanup-none",       STATE_CLEANUP,      STATE_FINALIZE},
    { "cleanup-fail",       STATE_CLEANUP,      STATE_FINALIZE},
    { "rc3-success",        STATE_FINALIZE,     STATE_SHUTDOWN },
    { "rc3-none",           STATE_FINALIZE,     STATE_SHUTDOWN },
    { "rc3-fail",           STATE_FINALIZE,     STATE_SHUTDOWN },
};

#define TABLE_LENGTH(t) (sizeof(t) / sizeof((t)[0]))

static void state_action (struct state_machine *s, broker_state_t state)
{
    int i;
    for (i = 0; i < TABLE_LENGTH (statetab); i++) {
        if (statetab[i].state == state) {
            if (statetab[i].action)
                statetab[i].action (s);
            break;
        }
    }
}

static const char *statestr (broker_state_t state)
{
    int i;
    for (i = 0; i < TABLE_LENGTH (statetab); i++) {
        if (statetab[i].state == state)
            return statetab[i].name;
    }
    return "unknown";
}

static broker_state_t state_next (broker_state_t current, const char *event)
{
    int i;
    for (i = 0; i < TABLE_LENGTH (nexttab); i++) {
        if (nexttab[i].current == current && !strcmp (event, nexttab[i].event))
            return nexttab[i].next;
    }
    return current;
}

static void action_init (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc1")) {
        if (runat_start (s->ctx->runat, "rc1", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc1");
            state_machine_post (s, "rc1-fail");
        }
    }
    else
        state_machine_post (s, "rc1-none");
}

static void action_join (struct state_machine *s)
{
    if (s->ctx->rank == 0)
        state_machine_post (s, "parent-none");
    else
        join_check_parent (s);
}

static void action_run (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc2")) {
        if (runat_start (s->ctx->runat, "rc2", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc2");
            state_machine_post (s, "rc2-fail");
        }
    }
    else
        state_machine_post (s, "rc2-none");
}

static void action_cleanup (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "cleanup")) {
        if (runat_start (s->ctx->runat, "cleanup", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start cleanup");
            state_machine_post (s, "cleanup-fail");
        }
    }
    else
        state_machine_post (s, "cleanup-none");
}

static void action_finalize (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc3")) {
        if (runat_start (s->ctx->runat, "rc3", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc3");
            state_machine_post (s, "rc3-fail");
        }
    }
    else
        state_machine_post (s, "rc3-none");
}

static void action_shutdown (struct state_machine *s)
{
    shutdown_instance (s->ctx->shutdown);
}

static void process_event (struct state_machine *s, const char *event)
{
    broker_state_t next_state;

    next_state = state_next (s->state, event);

    if (next_state != s->state) {
        flux_log (s->ctx->h,
                  LOG_INFO, "%s: %s->%s",
                  event,
                  statestr (s->state),
                  statestr (next_state));
        s->state = next_state;
        state_action (s, s->state);
        monitor_update (s->ctx->h, s->monitor.requests, s->state);
    }
    else {
        flux_log (s->ctx->h,
                  LOG_INFO,
                  "%s: ignored in %s",
                  event,
                  statestr (s->state));
    }
}

void state_machine_post (struct state_machine *s, const char *event)
{
    if (zlist_append (s->events, (char *)event) < 0)
        flux_log (s->ctx->h, LOG_ERR, "state_machine_post %s failed", event);
}

void state_machine_kill (struct state_machine *s, int signum)
{
    flux_t *h = s->ctx->h;

    switch (s->state) {
        case STATE_INIT:
            if (runat_abort (s->ctx->runat, "rc1") < 0)
                flux_log_error (h, "runat_abort rc1 (signal %d)", signum);
            break;
        case STATE_JOIN:
            state_machine_post (s, "parent-fail");
            break;
        case STATE_RUN:
            if (runat_is_defined (s->ctx->runat, "rc2")) {
                if (runat_abort (s->ctx->runat, "rc2") < 0)
                    flux_log_error (h, "runat_abort rc2 (signal %d)", signum);
            }
            else
                state_machine_post (s, "rc2-abort");
            break;
        case STATE_CLEANUP:
            if (runat_abort (s->ctx->runat, "cleanup") < 0)
                flux_log_error (h, "runat_abort cleanup (signal %d)", signum);
            break;
        case STATE_FINALIZE:
            (void)runat_abort (s->ctx->runat, "rc3");
            break;
        case STATE_NONE:
        case STATE_SHUTDOWN:
            flux_log (h,
                      LOG_INFO,
                      "ignored signal %d in %s",
                      signum,
                      statestr (s->state));
            break;
    }
}

static void runat_completion_cb (struct runat *r, const char *name, void *arg)
{
    struct state_machine *s = arg;
    int rc = 1;

    if (runat_get_exit_code (r, name, &rc) < 0)
        log_err ("runat_get_exit_code %s", name);

    if (!strcmp (name, "rc1")) {
        if (rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "rc1-success" : "rc1-fail");
    }
    else if (!strcmp (name, "rc2")) {
        if (rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "rc2-success" : "rc2-fail");
    }
    else if (!strcmp (name, "cleanup")) {
        if (rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "cleanup-success" : "cleanup-fail");
    }
    else if (!strcmp (name, "rc3")) {
        if (rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "rc3-success" : "rc3-fail");
    }
}

static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct state_machine *s = arg;

    if (zlist_size (s->events) > 0)
        flux_watcher_start (s->idle);
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct state_machine *s = arg;
    char *event;

    if ((event = zlist_pop (s->events))) {
        process_event (s, event);
        free (event);
    }
    flux_watcher_stop (s->idle);
}

/* Assumes local state is STATE_JOIN.
 * If parent has left STATE_JOIN, post parent-ready or parent-fail.
 */
static void join_check_parent (struct state_machine *s)
{
    if (s->monitor.parent_error)
        state_machine_post (s, "parent-fail");
    else if (s->monitor.parent_valid) {
        switch (s->monitor.parent_state) {
            case STATE_NONE:
            case STATE_JOIN:
                state_machine_post (s, "parent-ready");
                break;
            case STATE_INIT:
            case STATE_RUN:
            case STATE_CLEANUP:
            case STATE_FINALIZE:
            case STATE_SHUTDOWN:
                state_machine_post (s, "parent-fail");
                break;
        }
    }
}

static void monitor_update (flux_t *h, zlist_t *requests, broker_state_t state)
{
    const flux_msg_t *msg;

    msg = zlist_first (requests);
    while (msg) {
        if (flux_respond_pack (h, msg, "{s:i}", "state", state) < 0) {
            if (errno != EHOSTUNREACH)
                flux_log_error (h, "error responding to monitor request");
        }
        msg = zlist_next (requests);
    }
}

static void monitor_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct state_machine *s = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i}", "state", s->state) < 0)
        flux_log_error (h, "error responding to monitor request");
    if (flux_msg_is_streaming (msg)) {
        if (zlist_append (s->monitor.requests,
                          (flux_msg_t *)flux_msg_incref (msg)) < 0) {
            flux_msg_decref (msg);
            errno = ENOMEM;
            goto error;
        }
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to monitor request");
}

static void monitor_continuation (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;
    flux_t *h = s->ctx->h;
    int state;

    if (flux_rpc_get_unpack (f, "{s:i}", "state", &state) < 0) {
        flux_log_error (h, "monitor");
        s->monitor.parent_error = 1;
        return;
    }
    s->monitor.parent_state = state;
    s->monitor.parent_valid = 1;
    flux_future_reset (f);
    if (s->state == STATE_JOIN)
        join_check_parent (s);
}

static flux_future_t *monitor_parent (flux_t *h, void *arg)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h,
                        "state-machine.monitor",
                        NULL,
                        FLUX_NODEID_UPSTREAM,
                        FLUX_RPC_STREAMING)))
        return NULL;
    if (flux_future_then (f, -1, monitor_continuation, arg) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "state-machine.monitor", monitor_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void msglist_destroy (zlist_t *l)
{
    if (l) {
        const flux_msg_t *msg;
        while ((msg = zlist_pop (l)))
            flux_msg_decref (msg);
        zlist_destroy (&l);
    }
}

void state_machine_destroy (struct state_machine *s)
{
    if (s) {
        int saved_errno = errno;
        zlist_destroy (&s->events);
        flux_watcher_destroy (s->prep);
        flux_watcher_destroy (s->check);
        flux_watcher_destroy (s->idle);
        flux_msg_handler_delvec (s->handlers);
        flux_future_destroy (s->monitor.f);
        msglist_destroy (s->monitor.requests);
        free (s);
        errno = saved_errno;
    }
}

struct state_machine *state_machine_create (struct broker *ctx)
{
    struct state_machine *s;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (!(s = calloc (1, sizeof (*s))))
        return NULL;
    s->ctx = ctx;
    s->state = STATE_NONE;
    if (!(s->events = zlist_new ()))
        goto nomem;
    zlist_autofree (s->events);
    if (flux_msg_handler_addvec (ctx->h, htab, s, &s->handlers) < 0)
        goto error;
    s->prep = flux_prepare_watcher_create (r, prep_cb, s);
    s->check = flux_check_watcher_create (r, check_cb, s);
    s->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!s->prep || !s->check || !s->idle)
        goto nomem;
    flux_watcher_start (s->prep);
    flux_watcher_start (s->check);
    if (!(s->monitor.requests = zlist_new ()))
        goto nomem;
    if (ctx->rank > 0) {
        if (!(s->monitor.f = monitor_parent (ctx->h, s)))
            goto error;
    }
    return s;
nomem:
    errno = ENOMEM;
error:
    state_machine_destroy (s);
    return NULL;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
