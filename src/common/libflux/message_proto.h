/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_PROTO_H
#define _FLUX_CORE_MESSAGE_PROTO_H

/* PROTO consists of 4 byte prelude followed by a fixed length
 * array of u32's in network byte order.
 */
#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1

#define PROTO_OFF_MAGIC     0 /* 1 byte */
#define PROTO_OFF_VERSION   1 /* 1 byte */
#define PROTO_OFF_TYPE      2 /* 1 byte */
#define PROTO_OFF_FLAGS     3 /* 1 byte */
#define PROTO_OFF_U32_ARRAY 4

/* aux1
 *
 * request - nodeid
 * response - errnum
 * event - sequence
 * control - type
 *
 * aux2
 *
 * request - matchtag
 * response - matchtag
 * event - not used
 * control - status
 */
#define PROTO_IND_USERID    0
#define PROTO_IND_ROLEMASK  1
#define PROTO_IND_AUX1      2
#define PROTO_IND_AUX2      3

#define PROTO_U32_COUNT     4
#define PROTO_SIZE          4 + (PROTO_U32_COUNT * 4)

void msg_proto_setup (const flux_msg_t *msg, uint8_t *data, int len);

void proto_get_u32 (const uint8_t *data, int index, uint32_t *val);

#endif /* !_FLUX_CORE_MESSAGE_PROTO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

