/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_SUBSCRIBE_H
#define _SDBUS_SUBSCRIBE_H

#include <flux/core.h>

/* sdbus RPC for Subscribe and AddMatch method-calls.
 * The calls are made sequentially and the future is fulfilled when
 * both complete.
 * N.B. these are not direct method-calls.  They are RPCs to sdbus.call,
 * so when made from sdbus itself, they rely on the fact that RPCs to self
 * do work in broker modules.
 */
flux_future_t *sdbus_subscribe (flux_t *h);

#endif /* !_SDBUS_SUBSCRIBE_H */

// vi:ts=4 sw=4 expandtab
