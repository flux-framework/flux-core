
/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_JOURNAL_H
#define _FLUX_JOB_MANAGER_JOURNAL_H

#include <stdarg.h>
#include <flux/core.h>
#include <jansson.h>

#include "job-manager.h"

/* Process the event by sending to any listeners that request the
 * event and append to the journal history.
 */
int journal_process_event (struct journal *journal,
                           flux_jobid_t id,
                           const char *name,
                           json_t *entry);

void journal_ctx_destroy (struct journal *journal);
struct journal *journal_ctx_create (struct job_manager *ctx);

void journal_listeners_disconnect_rpc (flux_t *h,
                                       flux_msg_handler_t *mh,
                                       const flux_msg_t *msg,
                                       void *arg);

json_t *journal_get_stats (struct journal *journal);

#endif /* _FLUX_JOB_MANAGER_JOURNAL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

