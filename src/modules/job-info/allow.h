/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_ALLOW_H
#define _FLUX_JOB_INFO_ALLOW_H

#include <flux/core.h>

#include "job-info.h"

/* Determine if user who sent request 'msg' is allowed to
 * access job eventlog 's'.  Assume first event is the "submit"
 * event which records the job owner.  Will cache recently looked
 * up job owners in an LRU cache.
 */
int eventlog_allow (struct info_ctx *ctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    const char *s);

/* Determine if user who sent request 'msg' is allowed to access job
 * eventlog via LRU cache.  Returns 1 if access allowed, 0 if
 * indeterminate, -1 on error (including EPERM if access not allowed).
 * If 0 returned, typically that means the eventlog needs to be looked
 * up and then eventlog_allow() has to be called.
 */
int eventlog_allow_lru (struct info_ctx *ctx,
                        const flux_msg_t *msg,
                        flux_jobid_t id);

#endif /* ! _FLUX_JOB_INFO_ALLOW_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
