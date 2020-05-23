/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_ACQUIRE_H
#define _FLUX_RESOURCE_ACQUIRE_H

struct acquire *acquire_create (struct resource_ctx *ctx);
void acquire_destroy (struct acquire *acquire);

void acquire_disconnect (struct acquire *acquire, const flux_msg_t *msg);

#endif /* !_FLUX_RESOURCE_ACQUIRE_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
