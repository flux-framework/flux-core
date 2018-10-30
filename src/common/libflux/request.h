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

#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a request message with optional string payload.
 * If topic is non-NULL, assign the request topic string.
 * If s is non-NULL, assign the string payload or set to NULL if none
 * exists.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode (const flux_msg_t *msg, const char **topic,
                         const char **s);

/* Decode a request message with required json payload.  These functions use
 * jansson unpack style variable arguments for decoding the JSON object
 * payload directly.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_unpack (const flux_msg_t *msg, const char **topic,
                         const char *fmt, ...);

/* Decode a request message with optional raw payload.
 * If topic is non-NULL, assign the request topic string.
 * Data and len must be non-NULL, and will be assigned the payload.
 * If there is no payload, they will be assigned NULL and zero.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode_raw (const flux_msg_t *msg, const char **topic,
                             const void **data, int *len);

/* Encode a request message with optional string payload.
 * If s is non-NULL, assign the string payload.
 */
flux_msg_t *flux_request_encode (const char *topic, const char *s);

/* Encode a request message with optional raw payload.
 * If data is non-NULL, assign the raw payload.
 * Otherwise there will be no payload.
 */
flux_msg_t *flux_request_encode_raw (const char *topic,
                                     const void *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
