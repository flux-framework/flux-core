/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_SHUTDOWN_H
#define BROKER_SHUTDOWN_H

struct broker;

struct shutdown *shutdown_create (struct broker *ctx);
void shutdown_destroy (struct shutdown *shutdown);

#endif /* !BROKER_SHUTDOWN_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
