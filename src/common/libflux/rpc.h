/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include "handle.h"
#include "future.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLUX_RPC_NORESPONSE = 1,
    FLUX_RPC_STREAMING = 2,
};

flux_future_t *flux_rpc (flux_t *h,
                         const char *topic,
                         const char *s,
                         uint32_t nodeid,
                         int flags);

flux_future_t *flux_rpc_pack (flux_t *h,
                              const char *topic,
                              uint32_t nodeid,
                              int flags,
                              const char *fmt,
                              ...);

flux_future_t *flux_rpc_raw (flux_t *h,
                             const char *topic,
                             const void *data,
                             int len,
                             uint32_t nodeid,
                             int flags);

flux_future_t *flux_rpc_message (flux_t *h,
                                 const flux_msg_t *msg,
                                 uint32_t nodeid,
                                 int flags);

int flux_rpc_get (flux_future_t *f, const char **s);

int flux_rpc_get_unpack (flux_future_t *f, const char *fmt, ...);

int flux_rpc_get_raw (flux_future_t *f, const void **data, int *len);

/* Accessor for RPC matchtag (see RFC 6).
 */
uint32_t flux_rpc_get_matchtag (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
