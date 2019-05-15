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

#include "info.h"

/* Determine if user who sent request 'msg' is allowed to
 * access job eventlog 's'.  Assume first event is the "submit"
 * event which records the job owner.
 */
int eventlog_allow (struct info_ctx *ctx, const flux_msg_t *msg, const char *s);

#endif /* ! _FLUX_JOB_INFO_ALLOW_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
