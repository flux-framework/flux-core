/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_FREE_H
#define _FLUX_SCHEDUTIL_FREE_H

#include <flux/core.h>

#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Respond to a free request.
 */
int schedutil_free_respond (schedutil_t *util, const flux_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SCHEDUTIL_FREE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
