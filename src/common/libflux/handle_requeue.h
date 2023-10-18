/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_HANDLE_REQUEUE_H
#define _FLUX_CORE_HANDLE_REQUEUE_H

#include "handle.h"

/* Add 'msg' to the front of the receive queue, ahead of any messages already
 * there.  A reference is taken on 'msg' - it is not copied.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int handle_requeue_push_front (flux_t *h, const flux_msg_t *msg);

/* Add 'msg' to the back of the receive queue, behind any messages already
 * there.  A reference is taken on 'msg' - it is not copied.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int handle_requeue_push_back (flux_t *h, const flux_msg_t *msg);

#endif // !_FLUX_CORE_HANDLE_REQUEUE_H

// vi:ts=4 sw=4 expandtab
