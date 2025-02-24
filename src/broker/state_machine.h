/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_STATE_MACHINE_H
#define _BROKER_STATE_MACHINE_H

struct broker;

typedef enum {
    STATE_NONE,
    STATE_JOIN,
    STATE_INIT,             // rc1
    STATE_QUORUM,
    STATE_RUN,              // initial program
    STATE_CLEANUP,
    STATE_SHUTDOWN,
    STATE_FINALIZE,         // rc3
    STATE_GOODBYE,
    STATE_EXIT,
} broker_state_t;


struct state_machine *state_machine_create (struct broker *ctx);
void state_machine_destroy (struct state_machine *s);

void state_machine_post (struct state_machine *s, const char *event);

void state_machine_kill (struct state_machine *s, int signum);

int state_machine_shutdown (struct state_machine *s, flux_error_t *error);

void state_machine_sd_notify (struct state_machine *s, const char *status);

#endif /* !_BROKER_STATE_MACHINE_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
