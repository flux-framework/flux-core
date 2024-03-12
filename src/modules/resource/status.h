/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
 \************************************************************/

#ifndef _RESOURCE_STATUS_H
#define _RESOURCE_STATUS_H

struct status *status_create (struct resource_ctx *ctx);
void status_destroy (struct status *status);

#endif /* ! _RESOURCE_STATUS_H */

// vi:ts=4 sw=4 expandtab
