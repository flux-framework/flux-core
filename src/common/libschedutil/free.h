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

/* Decode a free request.
 * Returns 0 on success, -1 on failure with errno set.
 */
int schedutil_free_request_decode (const flux_msg_t *msg, flux_jobid_t *id);

/* Respond to a free request.
 */
int schedutil_free_respond (flux_t *h, const flux_msg_t *msg);

#endif /* !_FLUX_SCHEDUTIL_FREE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
