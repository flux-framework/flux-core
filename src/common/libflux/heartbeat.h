/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_HEARTBEAT
#    define _FLUX_CORE_HEARTBEAT

#    include "message.h"

#    ifdef __cplusplus
extern "C" {
#    endif

flux_msg_t *flux_heartbeat_encode (int epoch);
int flux_heartbeat_decode (const flux_msg_t *msg, int *epoch);

#    ifdef __cplusplus
}
#    endif

#endif /* !_FLUX_CORE_HEARTBEAT */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
