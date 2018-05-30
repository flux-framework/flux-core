/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* publisher.c - event publishing service on rank 0 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/base64.h"

#include "publisher.h"

struct sender {
    publisher_send_f send;
    void *arg;
    char name[32];
};

struct publisher {
    flux_t *h;
    flux_msg_handler_t **handlers;
    int seq;
    zlist_t *senders;
};

void publisher_destroy (struct publisher *pub)
{
    if (pub) {
        int saved_errno = errno;
        flux_msg_handler_delvec (pub->handlers);
        if (pub->senders) {
            struct sender *sender;
            while ((sender = zlist_pop (pub->senders)))
                free (sender);
            zlist_destroy (&pub->senders);
        }
        free (pub);
        errno = saved_errno;
    }
}

struct publisher *publisher_create (void)
{
    struct publisher *pub;

    if (!(pub = calloc (1, sizeof (*pub))))
        return NULL;
    if (!(pub->senders = zlist_new ())) {
        publisher_destroy (pub);
        return NULL;
    }
    return pub;
}

static flux_msg_t *encode_event (const char *topic, int flags,
                                 uint32_t rolemask, uint32_t userid,
                                 uint32_t seq, const char *src)
{
    flux_msg_t *msg;
    void *dst = NULL;
    int saved_errno;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        goto error;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto error;
    if (flux_msg_set_userid (msg, userid) < 0)
        goto error;
    if (flux_msg_set_rolemask (msg, rolemask) < 0)
        goto error;
    if (flux_msg_set_seq (msg, seq) < 0)
        goto error;
    if ((flags & FLUX_MSGFLAG_PRIVATE)) {
        if (flux_msg_set_private (msg) < 0)
            goto error;
    }
    if (src) { // optional payload
        int srclen = strlen (src);
        int dstlen = base64_decode_length (srclen);

        if (!(dst = malloc (dstlen)))
            goto error;
        if (base64_decode_block (dst, &dstlen, src, srclen) < 0) {
            errno = EPROTO;
            goto error;
        }
        if (flux_msg_set_payload (msg, dst, dstlen) < 0) {
            if (errno == EINVAL)
                errno = EPROTO;
            goto error;
        }
    }
    free (dst);
    return msg;
error:
    saved_errno = errno;
    free (dst);
    flux_msg_destroy (msg);
    errno = saved_errno;
    return NULL;
}

/* Broadcast event using all senders.
 * Log failure, but don't abort the event at this point.
 */
static void send_event (struct publisher *pub, const flux_msg_t *msg)
{
    struct sender *sender;

    sender = zlist_first (pub->senders);
    while (sender != NULL) {
        if (sender->send (sender->arg, msg) < 0)
            flux_log_error (pub->h, "%s: sender=%s",
                            __FUNCTION__, sender->name);
        sender = zlist_next (pub->senders);
    }
}

void pub_cb (flux_t *h, flux_msg_handler_t *mh,
             const flux_msg_t *msg, void *arg)
{
    struct publisher *pub = arg;
    const char *topic;
    const char *payload = NULL; // optional
    int flags;
    uint32_t rolemask, userid;
    flux_msg_t *event = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s s:i s?:s}",
                                        "topic", &topic,
                                        "flags", &flags,
                                        "payload", &payload) < 0)
        goto error;
    if ((flags & ~(FLUX_MSGFLAG_PRIVATE)) != 0) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (flux_msg_get_userid (msg, &userid) < 0)
        goto error;
    if (!(event = encode_event (topic, flags, rolemask, userid,
                                ++pub->seq, payload)))
        goto error_restore_seq;
    send_event (pub, event);
    if (flux_respond_pack (h, msg, "{s:i}", "seq", pub->seq) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
    return;
error_restore_seq:
    pub->seq--;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
}

int publisher_send (struct publisher *pub, const flux_msg_t *msg)
{
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_clear_route (cpy) < 0)
        goto error;
    if (flux_msg_set_seq (cpy, ++pub->seq) < 0)
        goto error_restore_seq;
    send_event (pub, cpy);
    flux_msg_destroy (cpy);
    return 0;
error_restore_seq:
    pub->seq--;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "event.pub",  pub_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};


int publisher_set_flux (struct publisher *pub, flux_t *h)
{
    pub->h = h;
    if (flux_msg_handler_addvec (h, htab, pub, &pub->handlers) < 0)
        return -1;
    return 0;
}

int publisher_set_sender (struct publisher *pub, const char *name,
                          publisher_send_f cb, void *arg)
{
    struct sender *sender;

    if (!(sender = calloc (1, sizeof (*sender))))
        return -1;
    sender->send = cb;
    sender->arg = arg;
    (void)snprintf (sender->name, sizeof (sender->name), "%s", name);
    if (zlist_append (pub->senders, sender) < 0) {
        free (sender);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
