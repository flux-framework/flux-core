/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* state_machine.c - manage broker life cycle
 */

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
#include "join.h"

struct state_machine {
    struct broker *ctx;
    broker_state_t state;

    flux_future_t *f_rmmod;

    zlist_t *events;    // queue of pending events
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;

    bool shutdown;
};

typedef void (*action_f)(struct state_machine *s);

struct state {
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
static void action_shutdown (struct state_machine *s);
static void action_finalize (struct state_machine *s);
static void action_exit (struct state_machine *s);

/* order assumes broker_state_t enum values can be used as array index */
static struct state statetab[] = {
    { "none",             NULL },
    { "join",             action_join },
    { "init",             action_init },
    { "run",              action_run },
    { "cleanup",          action_cleanup },
    { "shutdown",         action_shutdown },
    { "finalize",         action_finalize },
    { "exit",             action_exit },
};

static struct state_next nexttab[] = {
    { "start",              STATE_NONE,         STATE_JOIN },
    { "parent-ready",       STATE_JOIN,         STATE_INIT },
    { "parent-none",        STATE_JOIN,         STATE_INIT },
    { "parent-timeout",     STATE_JOIN,         STATE_SHUTDOWN },
    { "parent-fail",        STATE_JOIN,         STATE_SHUTDOWN },
    { "rc1-success",        STATE_INIT,         STATE_RUN },
    { "rc1-none",           STATE_INIT,         STATE_RUN },
    { "rc1-fail",           STATE_INIT,         STATE_SHUTDOWN },
    { "rc2-success",        STATE_RUN,          STATE_CLEANUP },
    { "rc2-fail",           STATE_RUN,          STATE_CLEANUP },
    { "rc2-abort",          STATE_RUN,          STATE_CLEANUP },
    { "rc2-none",           STATE_RUN,          STATE_RUN },
    { "cleanup-success",    STATE_CLEANUP,      STATE_SHUTDOWN },
    { "cleanup-none",       STATE_CLEANUP,      STATE_SHUTDOWN },
    { "cleanup-fail",       STATE_CLEANUP,      STATE_SHUTDOWN },
    { "children-complete",  STATE_SHUTDOWN,     STATE_FINALIZE },
    { "children-none",      STATE_SHUTDOWN,     STATE_FINALIZE },
    { "children-timeout",   STATE_SHUTDOWN,     STATE_FINALIZE },
    { "rc3-success",        STATE_FINALIZE,     STATE_EXIT },
    { "rc3-none",           STATE_FINALIZE,     STATE_EXIT },
    { "rc3-fail",           STATE_FINALIZE,     STATE_EXIT },
    { NULL, 0, 0 },
};

/* Look up next state given current state and event.
 */
static broker_state_t state_next (broker_state_t current, const char *event)
{
    int i;
    for (i = 0; nexttab[i].event != NULL; i++) {
        if (nexttab[i].current == current && !strcmp (event, nexttab[i].event))
            return nexttab[i].next;
    }
    return current;
}

/* Run action function for specified state.
 */
static void state_action (struct state_machine *s, broker_state_t state)
{
    assert (state < sizeof (statetab) / sizeof (statetab[0]));
    if (statetab[state].action)
        statetab[state].action (s);
}

/* Convert state enum to string.
 */
static const char *statestr (broker_state_t state)
{
    assert (state < sizeof (statetab) / sizeof (statetab[0]));
    return statetab[state].name;
}

/* Process one event.
 */
static void process_event (struct state_machine *s, const char *event)
{
    struct broker *ctx = s->ctx;
    broker_state_t next_state = state_next (s->state, event);

    if (next_state != s->state) {
        flux_log (ctx->h,
                  LOG_INFO, "%s: %s->%s",
                  event,
                  statestr (s->state),
                  statestr (next_state));
        s->state = next_state;
        state_action (s, s->state);
    }
    else {
        flux_log (ctx->h,
                  LOG_INFO,
                  "%s: ignored in %s",
                  event,
                  statestr (s->state));
    }
}

/* Broker received shutdown.all notification that the instance is shutting
 * down (see shutdown.c).  If pre-RUN, set flag so that action_run() will
 * immediately post rc2-abort when it is reached.  If post-RUN, we are already
 * shutting down so ignore.
 */
void state_machine_shutdown (struct state_machine *s)
{
    switch (s->state) {
        case STATE_NONE:
        case STATE_JOIN:
        case STATE_INIT:
            flux_log (s->ctx->h,
                      LOG_INFO,
                      "shutdown deferred in %s", statestr (s->state));
            s->shutdown = true;
            break;
        case STATE_RUN:
            if (runat_is_defined (s->ctx->runat, "rc2")) {
                if (runat_abort (s->ctx->runat, "rc2") < 0)
                    flux_log_error (s->ctx->h, "runat_abort rc2 (shutdown)");
            }
            else
                state_machine_post (s, "rc2-abort");
            break;
        case STATE_CLEANUP:
        case STATE_SHUTDOWN:
        case STATE_FINALIZE:
        case STATE_EXIT:
            flux_log (s->ctx->h,
                      LOG_INFO,
                      "shutdown: already in %s",
                      statestr (s->state));
            break;
    }
}

/* Broker received signal.  Unlike shutdown where current scripts are
 * allowed to complete and the goal is to terminate as nicely as possible,
 * signal may indicate more urgency (perhaps a script is hung), so try to
 * kill any running scripts and let the state machine handle it as a
 * script error.
 */
void state_machine_kill (struct state_machine *s, int signum)
{
    flux_t *h = s->ctx->h;

    switch (s->state) {
        case STATE_INIT:
            if (runat_abort (s->ctx->runat, "rc1") < 0)
                flux_log_error (h, "runat_abort rc1 (signal %d)", signum);
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
            if (runat_abort (s->ctx->runat, "rc1") < 0)
                flux_log_error (h, "runat_abort rc3 (signal %d)", signum);
            break;
        case STATE_NONE:
        case STATE_JOIN:
        case STATE_SHUTDOWN:
        case STATE_EXIT:
            flux_log (h,
                      LOG_INFO,
                      "ignored signal %d in %s",
                      signum,
                      statestr (s->state));
            break;
    }
}

/* Enqueue an event for the state machine.
 */
void state_machine_post (struct state_machine *s, const char *event)
{
    if (zlist_append (s->events, (char *)event) < 0)
        flux_log (s->ctx->h, LOG_ERR, "state_machine_post %s failed", event);
}

broker_state_t state_machine_get_state (struct state_machine *s)
{
    return s->state;
}

/* runat_start() script completed.
 * Make sure the broker exits wtih a nonzero exit code if any scripts fail.
 */
static void completion_cb (struct runat *r, const char *name, void *arg)
{
    struct state_machine *s = arg;
    int rc;

    runat_get_exit_code (r, name, &rc);
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

/* JOIN: send a join.wait-ready request to TBON parent.
 * On response, post parent-ready/fail/timeout.
 */
static void action_join (struct state_machine *s)
{
    if (s->ctx->rank > 0) {
        if (join_start (s->ctx->join) < 0) {
            flux_log_error (s->ctx->h, "join_start");
             state_machine_post (s, "parent-fail");
        }
    }
    else
        state_machine_post (s, "parent-none");
}

/* INIT: start rc1 script.
 * On completion, post rc1-success/fail.
 */
static void action_init (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc1")) {
        if (runat_start (s->ctx->runat, "rc1", completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc1");
            state_machine_post (s, "rc1-fail");
        }
    }
    else
        state_machine_post (s, "rc1-none");
}

/* RUN: start rc2 script (will only be defined on rank 0).
 * Post parent-ready event to child broker blocked in JOIN.
 * On completion, post rc2-success/fail.
 */
static void action_run (struct state_machine *s)
{
    if (s->shutdown)
        state_machine_post (s, "rc2-abort");
    else if (runat_is_defined (s->ctx->runat, "rc2")) {
        if (runat_start (s->ctx->runat, "rc2", completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc2");
            state_machine_post (s, "rc2-fail");
        }
    }
    else
        state_machine_post (s, "rc2-none");
    join_notify (s->ctx->join, STATE_RUN);
}

/* CLEANUP: start cleanup script (will only be defined on rank 0).
 * On completion, post cleanup-success/fail.
 */
static void action_cleanup (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "cleanup")) {
        if (runat_start (s->ctx->runat, "cleanup", completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start cleanup");
            state_machine_post (s, "cleanup-fail");
        }
    }
    else
        state_machine_post (s, "cleanup-none");
}

/* SHUTDOWN: start shutdown.
 * Post parent-fail event to any child brokers blocked in JOIN.
 * On child exit, post children-complete/timeout.
 */
static void action_shutdown (struct state_machine *s)
{
    shutdown_notify (s->ctx->shutdown);
    join_notify (s->ctx->join, STATE_SHUTDOWN);
}

/* FINALIZE: start rc3 script.
 * On completion, post rc3-success/fail.
 */
static void action_finalize (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc3")) {
        if (runat_start (s->ctx->runat, "rc3", completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc3");
            state_machine_post (s, "rc3-fail");
        }
    }
    else
        state_machine_post (s, "rc3-none");
}

static void rmmod_continuation (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;

    flux_reactor_stop (s->ctx->reactor);
}

static flux_future_t *rmmod (flux_t *h,
                             const char *name,
                             void *arg)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "cmb.rmmod",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name",
                             name)))
        return NULL;
    if (flux_future_then (f, -1, rmmod_continuation, arg) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

/* EXIT: unload connector-local then stop reactor.
 */
static void action_exit (struct state_machine *s)
{
    if (!(s->f_rmmod = rmmod (s->ctx->h, "connector-local", s))) {
        flux_log_error (s->ctx->h, "unloading connector-local");
        flux_reactor_stop (s->ctx->reactor);
    }
}

/* prep_cb() runs right before reactor calls poll(2).  If an event is
 * available, start the (no-op) idle watcher, which prevents poll from
 * blocking.  check_cb() runs right after poll(2) returns, processing
 * zero or one event, and stopping the idle watcher.
 */
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

void state_machine_destroy (struct state_machine *s)
{
    if (s) {
        int saved_errno = errno;
        flux_watcher_destroy (s->prep);
        flux_watcher_destroy (s->check);
        flux_watcher_destroy (s->idle);
        zlist_destroy (&s->events);
        flux_future_destroy (s->f_rmmod);
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
    s->prep = flux_prepare_watcher_create (r, prep_cb, s);
    s->check = flux_check_watcher_create (r, check_cb, s);
    s->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!s->prep || !s->check || !s->idle)
        goto nomem;
    flux_watcher_start (s->prep);
    flux_watcher_start (s->check);
    return s;
nomem:
    errno = ENOMEM;
    state_machine_destroy (s);
    return NULL;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
