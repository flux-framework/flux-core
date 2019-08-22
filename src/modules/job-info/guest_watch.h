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

void guest_watch_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg);

void guest_watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                            const flux_msg_t *msg, void *arg);

/* Cancel all lookups that match (sender, matchtag). */
void guest_watchers_cancel (struct info_ctx *ctx,
                            const char *sender, uint32_t matchtag);

void guest_watch_cleanup (struct info_ctx *ctx);

#endif /* ! _FLUX_JOB_INFO_EVENTLOG_GUEST_WATCH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
