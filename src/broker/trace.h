/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_TRACE_H
#define BROKER_TRACE_H

#include <flux/core.h>

/* Send trace info for 'msg' to all tracers in 'trace_requests'.
 */
void trace_module_msg (flux_t *h,
                       const char *prefix,
                       const char *module_name,
                       struct flux_msglist *trace_requests,
                       const flux_msg_t *msg);

void trace_overlay_msg (flux_t *h,
                        const char *prefix,
                        uint32_t overlay_peer,
                        struct flux_msglist *trace_requests,
                        const flux_msg_t *msg);

#endif /* !BROKER_TRACE_H */

// vi:ts=4 sw=4 expandtab
