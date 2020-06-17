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

const char *statestr (broker_state_t state)
{
    return (state == STATE_NONE ? "none" :
            state == STATE_INIT ? "init" :
            state == STATE_RUN ? "run" :
            state == STATE_CLEANUP ? "cleanup" :
            state == STATE_FINALIZE ? "finalize" :
            state == STATE_SHUTDOWN ? "shutdown" : "???");
}

broker_state_t state_next (broker_state_t state, const char *event)
{
    if (!strcmp (event, "wireup-complete")) {
        if (state == STATE_NONE)
            state = STATE_INIT;
    }
    else if (!strcmp (event, "rc1-fail")) {
        if (state == STATE_INIT)
            state = STATE_FINALIZE;
    }
    else if (!strcmp (event, "rc1-success")) {
        if (state == STATE_INIT)
            state = STATE_RUN;
    }
    else if (!strcmp (event, "rc2-fail")
          || !strcmp (event, "rc2-success")
          || !strcmp (event, "rc2-abort-noscript")) {
        if (state == STATE_RUN)
            state = STATE_CLEANUP;
    }
    else if (!strcmp (event, "cleanup-fail")
          || !strcmp (event, "cleanup-success")) {
        if (state == STATE_CLEANUP)
            state = STATE_FINALIZE;
    }
    else if (!strcmp (event, "rc3-fail")
          || !strcmp (event, "rc3-success")) {
        if (state == STATE_FINALIZE)
            state = STATE_SHUTDOWN;
    }
    return state;
}

void state_action (struct broker *ctx, broker_state_t state)
{
    switch (state) {
        case STATE_INIT:
            if (runat_start (ctx->runat, "rc1") < 0)
                state_machine (ctx, "rc1-success");
            break;
        case STATE_RUN:
            if (runat_start (ctx->runat, "rc2") < 0)
                flux_log (ctx->h, LOG_DEBUG, "no initial program defined");
            break;
        case STATE_CLEANUP:
            if (runat_start (ctx->runat, "cleanup") < 0)
                state_machine (ctx, "cleanup-success");
            break;
        case STATE_FINALIZE:
            if (runat_start (ctx->runat, "rc3") < 0)
                state_machine (ctx, "rc3-success");
            break;
        case STATE_SHUTDOWN:
            shutdown_instance (ctx->shutdown);
            break;
        case STATE_NONE:
            break;
    }
}

void state_machine (struct broker *ctx, const char *event)
{
    broker_state_t next_state;

    next_state = state_next (ctx->state, event);

    if (next_state != ctx->state) {
        flux_log (ctx->h,
                  LOG_INFO, "%s: %s->%s",
                  event,
                  statestr (ctx->state),
                  statestr (next_state));
        ctx->state = next_state;
        state_action (ctx, ctx->state);
    }
    else {
        flux_log (ctx->h,
                  LOG_INFO,
                  "%s: ignored in %s",
                  event,
                  statestr (ctx->state));
    }
}

void state_abort (struct broker *ctx)
{
    switch (ctx->state) {
        case STATE_NONE:
            break;
        case STATE_INIT:
            (void)runat_abort (ctx->runat, "rc1");
            break;
        case STATE_RUN:
            if (runat_abort (ctx->runat, "rc2") < 0)
                state_machine (ctx, "rc2-abort-noscript");
            break;
        case STATE_CLEANUP:
            (void)runat_abort (ctx->runat, "cleanup");
            break;
        case STATE_FINALIZE:
            (void)runat_abort (ctx->runat, "rc3");
            break;
        case STATE_SHUTDOWN:
            break;
    }
}

/*
 * vi:ts=4 sw=4 expandtab
 */
