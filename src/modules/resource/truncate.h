/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_TRUNCATE_H
#define _FLUX_RESOURCE_TRUNCATE_H

void truncate_eventlog (struct resource_ctx *ctx,
                        const json_t *eventlog,
                        double checkpoint_timestamp,
                        double history);

#endif /* !_FLUX_RESOURCE_TRUNCATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
