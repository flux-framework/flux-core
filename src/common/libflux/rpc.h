/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include "handle.h"
#include "future.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLUX_RPC_NORESPONSE = 1,
};

flux_future_t *flux_rpc (flux_t *h, const char *topic, const char *s,
                         uint32_t nodeid, int flags);

flux_future_t *flux_rpc_pack (flux_t *h, const char *topic, uint32_t nodeid,
                              int flags, const char *fmt, ...);

flux_future_t *flux_rpc_raw (flux_t *h, const char *topic,
                             const void *data, int len,
                             uint32_t nodeid, int flags);

flux_future_t *flux_rpc_message (flux_t *h, const flux_msg_t *msg,
                                 uint32_t nodeid, int flags);

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
