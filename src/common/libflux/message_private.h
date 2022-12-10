/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_PRIVATE_H
#define _FLUX_CORE_MESSAGE_PRIVATE_H

#include <stdint.h>
#include <jansson.h>

/* czmq and ccan both define streq */
#ifdef streq
#undef streq
#endif
#include "src/common/libccan/ccan/list/list.h"

#include "message_proto.h"

struct flux_msg {
    // optional route list, if FLUX_MSGFLAG_ROUTE
    struct list_head routes;
    int routes_len;     /* to avoid looping */

    // optional topic frame, if FLUX_MSGFLAG_TOPIC
    char *topic;

    // optional payload frame, if FLUX_MSGFLAG_PAYLOAD
    void *payload;
    size_t payload_size;

    // required proto frame data
    struct proto proto;

    json_t *json;
    char *lasterr;
    struct aux_item *aux;
    int refcount;
};

#define msgtype_is_valid(tp) \
    ((tp) == FLUX_MSGTYPE_REQUEST || (tp) == FLUX_MSGTYPE_RESPONSE \
     || (tp) == FLUX_MSGTYPE_EVENT || (tp) == FLUX_MSGTYPE_CONTROL)

#define msg_typeof(msg)         ((msg)->proto.type)
#define msg_type_is_valid(msg)  (msgtype_is_valid (msg_typeof (msg)))
#define msg_is_request(msg)     (msg_typeof(msg) == FLUX_MSGTYPE_REQUEST)
#define msg_is_response(msg)    (msg_typeof(msg) == FLUX_MSGTYPE_RESPONSE)
#define msg_is_event(msg)       (msg_typeof(msg) == FLUX_MSGTYPE_EVENT)
#define msg_is_control(msg)     (msg_typeof(msg) == FLUX_MSGTYPE_CONTROL)

#endif /* !_FLUX_CORE_MESSAGE_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

