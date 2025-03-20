/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_GUEST_WATCH_H
#define _FLUX_JOB_INFO_GUEST_WATCH_H

#include <flux/core.h>

#include "job-info.h"

int guest_watch (struct info_ctx *ctx,
                 const flux_msg_t *msg,
                 flux_jobid_t id,
                 const char *path,
                 int flags);

/* Cancel all lookups that match msg.
 * match credentials & matchtag if cancel true
 * match credentials if cancel false
 */
void guest_watchers_cancel (struct info_ctx *ctx,
                            const flux_msg_t *msg,
                            bool cancel);

int guest_watch_setup (struct info_ctx *ctx);

void guest_watch_cleanup (struct info_ctx *ctx);

#endif /* ! _FLUX_JOB_INFO_GUEST_WATCH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
