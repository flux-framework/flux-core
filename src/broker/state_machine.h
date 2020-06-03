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
    STATE_INIT,             // rc1
    STATE_RUN,              // initial program
    STATE_CLEANUP,
    STATE_FINALIZE,         // rc3
    STATE_SHUTDOWN,
} broker_state_t;

void state_machine (struct broker *ctx, const char *event);

void state_abort (struct broker *ctx);

#endif /* !_BROKER_STATE_MACHINE_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
