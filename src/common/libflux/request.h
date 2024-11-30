/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a request message with optional string payload.
 * If topic is non-NULL, assign the request topic string.
 * If s is non-NULL, assign the string payload or set to NULL if none
 * exists.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode (const flux_msg_t *msg,
                         const char **topic,
                         const char **s);

/* Decode a request message with required json payload.  These functions use
 * jansson unpack style variable arguments for decoding the JSON object
 * payload directly.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_unpack (const flux_msg_t *msg,
                         const char **topic,
                         const char *fmt,
                         ...);

/* Decode a request message with optional raw payload.
 * If topic is non-NULL, assign the request topic string.
 * Data and len must be non-NULL, and will be assigned the payload.
 * If there is no payload, they will be assigned NULL and zero.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode_raw (const flux_msg_t *msg,
                             const char **topic,
                             const void **data,
                             int *len);

/* Encode a request message with optional string payload.
 * If s is non-NULL, assign the string payload.
 */
flux_msg_t *flux_request_encode (const char *topic, const char *s);

/* Encode a request message with optional raw payload.
 * If data is non-NULL, assign the raw payload.
 * Otherwise there will be no payload.
 */
flux_msg_t *flux_request_encode_raw (const char *topic,
                                     const void *data,
                                     int len);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
