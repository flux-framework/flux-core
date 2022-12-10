/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* message_proto.c - marshal RFC 3 PROTO frame */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>

#include "ccan/pushpull/pushpull.h"

#include "message_proto.h"

/* Realloc(3) replacement for push() that simply returns the pointer, unless
 * the requested length exceeds the fixed size of an encoded proto frame.
 * It assumes proto_encode() has received a static buffer >= PROTO_SIZE.
 */
static void *proto_realloc (void *ptr, size_t len)
{
    if (len > PROTO_SIZE)
        return NULL;
    return ptr;
}

int proto_encode (const struct proto *proto, void *buf, size_t size)
{
    char *cp = buf;
    size_t len = 0;

    push_set_realloc (proto_realloc);

    if (size < PROTO_SIZE
        || !push_u8 (&cp, &len, PROTO_MAGIC)
        || !push_u8 (&cp, &len, PROTO_VERSION)
        || !push_u8 (&cp, &len, proto->type)
        || !push_u8 (&cp, &len, proto->flags)
        || !push_u32 (&cp, &len, proto->userid)
        || !push_u32 (&cp, &len, proto->rolemask)
        || !push_u32 (&cp, &len, proto->aux1)
        || !push_u32 (&cp, &len, proto->aux2)
        || len < size) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int proto_decode (struct proto *proto, const void *buf, size_t size)
{
    const char *cp = buf;
    size_t len = size;
    uint8_t magic, version;

    if (!pull_u8 (&cp, &len, &magic)
        || magic != PROTO_MAGIC
        || !pull_u8 (&cp, &len, &version)
        || version != PROTO_VERSION
        || !pull_u8 (&cp, &len, &proto->type)
        || !pull_u8 (&cp, &len, &proto->flags)
        || !pull_u32 (&cp, &len, &proto->userid)
        || !pull_u32 (&cp, &len, &proto->rolemask)
        || !pull_u32 (&cp, &len, &proto->aux1)
        || !pull_u32 (&cp, &len, &proto->aux2)
        || len > 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
