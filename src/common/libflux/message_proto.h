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

#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1
#define PROTO_SIZE          20

struct proto {
    uint8_t type;
    uint8_t flags;
    uint32_t userid;
    uint32_t rolemask;
    union {
        uint32_t nodeid;  // request
        uint32_t sequence; // event
        uint32_t errnum; // response
        uint32_t control_type; // control
        uint32_t aux1; // common accessor
    };
    union {
        uint32_t matchtag; // request, response
        uint32_t control_status; // control
        uint32_t aux2; // common accessor
    };
};

int proto_encode (const struct proto *proto, void *buf, size_t size);
int proto_decode (struct proto *proto, const void *buf, size_t size);

#endif /* !_FLUX_CORE_MESSAGE_PROTO_H */

// vi:ts=4 sw=4 expandtab
