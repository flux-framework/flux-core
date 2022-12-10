/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>

#include "message.h"
#include "message_private.h"
#include "message_proto.h"

static void proto_set_u32 (uint8_t *data, int index, uint32_t val)
{
    uint32_t x = htonl (val);
    int offset = PROTO_OFF_U32_ARRAY + index * 4;
    memcpy (&data[offset], &x, sizeof (x));
}

void msg_proto_setup (const flux_msg_t *msg, uint8_t *data, int len)
{
    assert (len >= PROTO_SIZE);
    assert (msg->proto.type != FLUX_MSGTYPE_ANY);
    memset (data, 0, len);
    data[PROTO_OFF_MAGIC] = PROTO_MAGIC;
    data[PROTO_OFF_VERSION] = PROTO_VERSION;
    data[PROTO_OFF_TYPE] = msg->proto.type;
    data[PROTO_OFF_FLAGS] = msg->proto.flags;
    proto_set_u32 (data, PROTO_IND_USERID, msg->proto.userid);
    proto_set_u32 (data, PROTO_IND_ROLEMASK, msg->proto.rolemask);
    proto_set_u32 (data, PROTO_IND_AUX1, msg->proto.aux1);
    proto_set_u32 (data, PROTO_IND_AUX2, msg->proto.aux2);
}

void proto_get_u32 (const uint8_t *data, int index, uint32_t *val)
{
    uint32_t x;
    int offset = PROTO_OFF_U32_ARRAY + index * 4;
    memcpy (&x, &data[offset], sizeof (x));
    *val = ntohl (x);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

