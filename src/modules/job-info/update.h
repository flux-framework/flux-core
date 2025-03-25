/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_UPDATE_H
#define _FLUX_JOB_INFO_UPDATE_H

#include <flux/core.h>
#include <jansson.h>

void update_watch_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg);

void update_watch_cancel_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg);

/* returns 1 on found, 0 if not, -1 on error */
int update_watch_get_cached (struct info_ctx *ctx,
                             flux_jobid_t id,
                             const char *key,
                             json_t **current_object);

/* Cancel all update watches that match msg.
 * match credentials & matchtag if cancel true
 * match credentials if cancel false
 */
void update_watchers_cancel (struct info_ctx *ctx,
                             const flux_msg_t *msg,
                             bool cancel);

int update_watch_setup (struct info_ctx *ctx);

void update_watch_cleanup (struct info_ctx *ctx);

int update_watch_count (struct info_ctx *ctx);

#endif /* ! _FLUX_JOB_INFO_UPDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
