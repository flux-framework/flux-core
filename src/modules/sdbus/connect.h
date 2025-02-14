/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_CONNECT_H
#define _SDBUS_CONNECT_H

#include <flux/core.h>

/* Connect the sd-bus with retries.  When the connect is successful, the
 * future is fulfilled with an sd_bus object.  When the future is destroyed,
 * the sd_bus object is flushed, closed, and unreferenced.
 *
 * If first_time=true, connect immediately; otherwise, wait retry_min secs.
 * If the initial connect is unsuccessful, retry in retry_min secs.  If that
 * is unsuccessful, back off exponentially, leveling off at retry_max secs
 * between attempts.
 *
 * Connect attempt successes and failures are logged at LOG_INFO level.
 */
flux_future_t *sdbus_connect (flux_t *h,
                              bool first_time,
                              double retry_min,
                              double retry_max,
                              bool system_bus);

#endif /* !_SDBUS_CONNECT_H */

// vi:ts=4 sw=4 expandtab
