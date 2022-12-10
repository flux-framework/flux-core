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
#include "message_route.h"
#include "message_proto.h"
#include "message_iovec.h"

int iovec_to_msg (flux_msg_t *msg,
                  struct msg_iovec *iov,
                  int iovcnt)
{
    unsigned int index = 0;
    const uint8_t *proto_data;
    size_t proto_size;

    assert (msg);
    assert (iov);

    if (!iovcnt) {
        errno = EPROTO;
        return -1;
    }

    /* proto frame is last frame */
    proto_data = iov[iovcnt - 1].data;
    proto_size = iov[iovcnt - 1].size;
    if (proto_size < PROTO_SIZE
        || proto_data[PROTO_OFF_MAGIC] != PROTO_MAGIC
        || proto_data[PROTO_OFF_VERSION] != PROTO_VERSION) {
        errno = EPROTO;
        return -1;
    msg->proto.type = proto_data[PROTO_OFF_TYPE];
    if (!msg_type_is_valid (msg)) {
        errno = EPROTO;
        return -1;
    }
    msg->proto.flags = proto_data[PROTO_OFF_FLAGS];

    if (msg_has_route (msg)) {
        /* On first access index == 0 && iovcnt > 0 guaranteed
         * Re-add check if code changes. */
        /* if (index == iovcnt) { */
        /*     errno = EPROTO; */
        /*     return -1; */
        /* } */
        while ((index < iovcnt) && iov[index].size > 0) {
            if (msg_route_append (msg,
                                  (char *)iov[index].data,
                                  iov[index].size) < 0)
                return -1;
            index++;
        }
        if (index < iovcnt)
            index++;
    }
    if (msg_has_topic (msg)) {
        if (index == iovcnt) {
            errno = EPROTO;
            return -1;
        }
        if (!(msg->topic = strndup ((char *)iov[index].data,
                                    iov[index].size)))
            return -1;
        if (index < iovcnt)
            index++;
    }
    if (msg_has_payload (msg)) {
        if (index == iovcnt) {
            errno = EPROTO;
            return -1;
        }
        msg->payload_size = iov[index].size;
        if (!(msg->payload = malloc (msg->payload_size)))
            return -1;
        memcpy (msg->payload, iov[index].data, msg->payload_size);
        if (index < iovcnt)
            index++;
    }
    /* proto frame required */
    if (index == iovcnt) {
        errno = EPROTO;
        return -1;
    }
    proto_get_u32 (proto_data, PROTO_IND_USERID, &msg->proto.userid);
    proto_get_u32 (proto_data, PROTO_IND_ROLEMASK, &msg->proto.rolemask);
    proto_get_u32 (proto_data, PROTO_IND_AUX1, &msg->proto.aux1);
    proto_get_u32 (proto_data, PROTO_IND_AUX2, &msg->proto.aux2);
    return 0;
}

int msg_to_iovec (const flux_msg_t *msg,
                  uint8_t *proto,
                  int proto_len,
                  struct msg_iovec **iovp,
                  int *iovcntp)
{
    struct msg_iovec *iov = NULL;
    int index;
    int frame_count;

    /* msg never completed initial setup */
    if (!msg_type_is_valid (msg)) {
        errno = EPROTO;
        return -1;
    }

    if ((frame_count = flux_msg_frames (msg)) < 0)
        return -1;

    assert (frame_count);

    if (!(iov = malloc (frame_count * sizeof (*iov))))
        return -1;

    index = frame_count - 1;

    assert (proto_len >= PROTO_SIZE);
    msg_proto_setup (msg, proto, proto_len);
    iov[index].data = proto;
    iov[index].size = PROTO_SIZE;
    if (msg_has_payload (msg)) {
        index--;
        assert (index >= 0);
        iov[index].data = msg->payload;
        iov[index].size = msg->payload_size;
    }
    if (msg_has_topic (msg)) {
        index--;
        assert (index >= 0);
        iov[index].data = msg->topic;
        iov[index].size = strlen (msg->topic);
    }
    if (msg_has_route (msg)) {
        struct route_id *r = NULL;
        /* delimeter */
        index--;
        assert (index >= 0);
        iov[index].data = NULL;
        iov[index].size = 0;
        list_for_each_rev (&msg->routes, r, route_id_node) {
            index--;
            assert (index >= 0);
            iov[index].data = r->id;
            iov[index].size = strlen (r->id);
        }
    }
    (*iovp) = iov;
    (*iovcntp) = frame_count;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

