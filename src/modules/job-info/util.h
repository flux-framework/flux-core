/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_UTIL_H
#define _FLUX_JOB_INFO_UTIL_H

#include <flux/core.h>

/* we want to copy credentials, etc. from the original
 * message when we send RPCs to other job-info targets.
 */
flux_msg_t *cred_msg_pack (const char *topic,
                           struct flux_msg_cred cred,
                           const char *fmt,
                           ...);

#endif /* ! _FLUX_JOB_INFO_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
