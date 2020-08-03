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

struct state_machine {
    struct broker *ctx;
    broker_state_t state;
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

static void action_init (struct state_machine *s);
static void action_run (struct state_machine *s);
static void action_cleanup (struct state_machine *s);
static void action_finalize (struct state_machine *s);
static void action_shutdown (struct state_machine *s);

static void runat_completion_cb (struct runat *r, const char *name, void *arg);

/* order assumes broker_state_t enum values can be used as array index */
static struct state statetab[] = {
    { "none",             NULL },
    { "init",             action_init },
    { "run",              action_run },
    { "cleanup",          action_cleanup },
    { "finalize",         action_finalize },
    { "shutdown",         action_shutdown },
};

static struct state_next nexttab[] = {
    { "wireup-complete",    STATE_NONE,         STATE_INIT },
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
    { NULL, 0, 0 },
};

static void state_action (struct state_machine *s, broker_state_t state)
{
    if (statetab[state].action)
        statetab[state].action (s);
}

static const char *statestr (broker_state_t state)
{
    return statetab[state].name;
}

static broker_state_t state_next (broker_state_t current, const char *event)
{
    int i;
    for (i = 0; nexttab[i].event != NULL; i++) {
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

void state_machine_post (struct state_machine *s, const char *event)
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
    }
    else {
        flux_log (s->ctx->h,
                  LOG_INFO,
                  "%s: ignored in %s",
                  event,
                  statestr (s->state));
    }
}

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

void state_machine_destroy (struct state_machine *s)
{
    if (s) {
        int saved_errno = errno;
        free (s);
        errno = saved_errno;
    }
}

struct state_machine *state_machine_create (struct broker *ctx)
{
    struct state_machine *s;

    if (!(s = calloc (1, sizeof (*s))))
        return NULL;
    s->ctx = ctx;
    s->state = STATE_NONE;

    return s;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
