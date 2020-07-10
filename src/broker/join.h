/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_JOIN_H_
#define _BROKER_JOIN_H_

struct broker;

struct join *join_create (struct broker *ctx);
void join_destroy (struct join *join);

/* Call when broker state transitions into, or past, RUN state.
 */
void join_notify (struct join *join, broker_state_t state);

/* Initiate join request to TBON parent.
 */
int join_start (struct join *join);

#endif /* !_BROKER_JOIN_H_ */

/*
 * vi:ts=4 sw=4 expandtab
 */
