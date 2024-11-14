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
#include <flux/core.h>

#include "message_private.h"
#include "message_route.h"
#include "message_proto.h"
#include "message_iovec.h"

flux_msg_t *iovec_to_msg (struct msg_iovec *iov, int iovcnt)
{
    unsigned int index = 0;
    flux_msg_t *msg;

    assert (iov);

    if (iovcnt < 1) {
        errno = EPROTO;
        return NULL;
    }
    if (!(msg = msg_create ()))
        return NULL;

    /* proto frame is last frame */
    if (proto_decode (&msg->proto,
                      iov[iovcnt - 1].data,
                      iov[iovcnt - 1].size) < 0)
        goto error;
    if (!msg_type_is_valid (msg)) {
        errno = EPROTO;
        goto error;
    }
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
                goto error;
            index++;
        }
        if (index < iovcnt)
            index++;
    }
    if (msg_has_topic (msg)) {
        if (index == iovcnt) {
            errno = EPROTO;
            goto error;
        }
        if (!(msg->topic = strndup ((char *)iov[index].data,
                                    iov[index].size)))
            goto error;
        if (index < iovcnt)
            index++;
    }
    if (msg_has_payload (msg)) {
        if (index == iovcnt) {
            errno = EPROTO;
            goto error;
        }
        msg->payload_size = iov[index].size;
        if (!(msg->payload = malloc (msg->payload_size)))
            goto error;
        memcpy (msg->payload, iov[index].data, msg->payload_size);
        if (index < iovcnt)
            index++;
    }
    /* proto frame required */
    if (index == iovcnt) {
        errno = EPROTO;
        goto error;
    }
    return msg;
error:
    flux_msg_decref (msg);
    return NULL;
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
    if (proto_encode (&msg->proto, proto, proto_len) < 0)
        return -1;

    if ((frame_count = msg_frames (msg)) < 0)
        return -1;

    assert (frame_count);

    if (!(iov = malloc (frame_count * sizeof (*iov))))
        return -1;

    index = frame_count - 1;

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
        /* delimiter */
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

