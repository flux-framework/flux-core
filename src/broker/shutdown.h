/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_SHUTDOWN_H
#define _BROKER_SHUTDOWN_H

struct shutdown;

struct shutdown *shutdown_create (struct broker *ctx);
void shutdown_destroy (struct shutdown *s);

/* Call when broker transitions into SHUTDOWN state.
 */
void shutdown_notify (struct shutdown *s);

#endif /* !_BROKER_SHUTDOWN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
