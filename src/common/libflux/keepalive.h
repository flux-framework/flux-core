/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KEEPALIVE_H
#define _FLUX_CORE_KEEPALIVE_H

#ifdef __cplusplus
extern "C" {
#endif

flux_msg_t *flux_keepalive_encode (int errnum, int status);

int flux_keepalive_decode (const flux_msg_t *msg, int *errnum, int *status);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KEEPALIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
