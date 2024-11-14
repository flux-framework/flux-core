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
#include <flux/core.h>

#include "ccan/pushpull/pushpull.h"
#include "ccan/endian/endian.h"

#include "message_proto.h"

/* push/pull functions that use network byte order (big endian)
 */

static bool be_pull_u32 (const char **p, size_t *max_len, uint32_t *val)
{
    beint32_t v;

    if (pull_bytes (p, max_len, &v, sizeof (v))) {
        if (val)
            *val = be32_to_cpu (v);
        return true;
    }
    return false;
}

static bool be_push_u32(char **p, size_t *len, uint32_t val)
{
    beint32_t v = cpu_to_be32 (val);
    return push_bytes (p, len, &v, sizeof (v));
}

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
        || !be_push_u32 (&cp, &len, proto->userid)
        || !be_push_u32 (&cp, &len, proto->rolemask)
        || !be_push_u32 (&cp, &len, proto->aux1)
        || !be_push_u32 (&cp, &len, proto->aux2)
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
        || !be_pull_u32 (&cp, &len, &proto->userid)
        || !be_pull_u32 (&cp, &len, &proto->rolemask)
        || !be_pull_u32 (&cp, &len, &proto->aux1)
        || !be_pull_u32 (&cp, &len, &proto->aux2)
        || len > 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
