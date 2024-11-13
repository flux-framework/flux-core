/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_SYNC_H
#define _FLUX_CORE_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Synchronize future to the system heartbeat.
 * Set minimum > 0. to establish a minimum time between fulfillments.
 * Use a continuation timeout to establish a maximum time between fulfillments.
 */
flux_future_t *flux_sync_create (flux_t *h, double minimum);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_SYNC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
