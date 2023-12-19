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

#include "ccan/list/list.h"

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
    struct list_node list; // for use by msg_deque container only
};

flux_msg_t *msg_create (void);

int msg_frames (const flux_msg_t *msg);

#define msgtype_is_valid(tp) \
    ((tp) == FLUX_MSGTYPE_REQUEST || (tp) == FLUX_MSGTYPE_RESPONSE \
     || (tp) == FLUX_MSGTYPE_EVENT || (tp) == FLUX_MSGTYPE_CONTROL)

#define msg_typeof(msg)         ((msg)->proto.type)
#define msg_type_is_valid(msg)  (msgtype_is_valid (msg_typeof (msg)))
#define msg_is_request(msg)     (msg_typeof(msg) == FLUX_MSGTYPE_REQUEST)
#define msg_is_response(msg)    (msg_typeof(msg) == FLUX_MSGTYPE_RESPONSE)
#define msg_is_event(msg)       (msg_typeof(msg) == FLUX_MSGTYPE_EVENT)
#define msg_is_control(msg)     (msg_typeof(msg) == FLUX_MSGTYPE_CONTROL)

#define msg_has_flag(msg,flag)  ((msg)->proto.flags & (flag))
#define msg_set_flag(msg,flag)  ((msg)->proto.flags |= (flag))
#define msg_clear_flag(msg,flag) \
                                ((msg)->proto.flags &= ~(flag))
#define msg_has_topic(msg)      (msg_has_flag(msg, FLUX_MSGFLAG_TOPIC))
#define msg_has_payload(msg)    (msg_has_flag(msg, FLUX_MSGFLAG_PAYLOAD))
#define msg_has_noresponse(msg) (msg_has_flag(msg, FLUX_MSGFLAG_NORESPONSE))
#define msg_has_route(msg)      (msg_has_flag(msg, FLUX_MSGFLAG_ROUTE))
#define msg_has_upstream(msg)   (msg_has_flag(msg, FLUX_MSGFLAG_UPSTREAM))
#define msg_has_private(msg)    (msg_has_flag(msg, FLUX_MSGFLAG_PRIVATE))
#define msg_has_streaming(msg)  (msg_has_flag(msg, FLUX_MSGFLAG_STREAMING))
#define msg_has_user1(msg)      (msg_has_flag(msg, FLUX_MSGFLAG_USER1))

#define msgflags_is_valid(fl) \
    (((fl) & ~(FLUX_MSGFLAG_TOPIC | FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_ROUTE \
     | FLUX_MSGFLAG_UPSTREAM | FLUX_MSGFLAG_PRIVATE | FLUX_MSGFLAG_STREAMING \
     | FLUX_MSGFLAG_NORESPONSE | FLUX_MSGFLAG_USER1)) == 0 \
     && !(((fl) & FLUX_MSGFLAG_NORESPONSE) && ((fl) & FLUX_MSGFLAG_STREAMING)))
#define msg_flags_is_valid(msg) (msgflags_is_valid ((msg)->proto.flags))

#endif /* !_FLUX_CORE_MESSAGE_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

