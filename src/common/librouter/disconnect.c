/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* disconnect.c - cache and then send disconnect messages
 *
 * A client may disconnect with state held in various services
 * that it has sent requests to.  This module tracks all the services that a
 * client has used and send a disconnect request to all of them when
 * the client exits.
 *
 * As a client sends requests, the router that is forwarding them calls
 * disconnect_arm() with the request message.  On first call for a
 * a given (service,nodied,upstream-flag) tuple, the disconnect hash is
 * primed with a new disconnect message.  On subsequent calls for the
 * same tuple, the call quickly returns success.
 *
 * When the client disconnects, the router should call disconnect_destroy()
 * which causes the registered callback to be invoked for each disconnect
 * message in the hash.  The callback is expected to forward the disconnect
 * message in the same manner as the original request.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "disconnect.h"

/* Since this code is performance critical, we impose a maximum topic string
 * size and and use static buffers.
 * N.B. consider adding this limit to RFC 3.
 */
#define TOPIC_BUFSIZE   256

#define HASHKEY_BUFSIZE (TOPIC_BUFSIZE + 64)

struct disconnect {
    zhashx_t *hash;
    disconnect_send_f cb;
    void *arg;
};

// N.B. zhashx_destructor_fn footprint
static void hash_destructor (void **item)
{
    if (item) {
        flux_msg_t *msg = *item;
        flux_msg_destroy (msg);
        *item = NULL;
    }
}

/* Build disconnect topic from request topic.
 * If msg topic is foo, disconnect topic is disconnect.
 * If msg topic is foo.bar, disconnect topic is foo.disconnect.
 * If msg topic is foo.bar.baz, disconnect topic is foo.bar.disconnect.
 * Returns number of bytes written to buf (excluding \0) or -1 on error.
 */
int disconnect_topic (const char *topic, char *buf, int len)
{
    const char *p;
    int service_len;
    int used;

    if (!topic || !buf) {
        errno = EINVAL;
        return -1;
    }
    if (!(p = strrchr (topic, '.')))
        used = snprintf (buf,
                         len,
                         "disconnect");
    else {
        service_len = p - topic;
        used = snprintf (buf,
                         len,
                         "%.*s.disconnect",
                         service_len,
                         topic);
    }
    if (used >= len) {
        errno = EINVAL;
        return -1;
    }
    return used;
}

/* Build a hash key for the disconnect message consisting of
 *   distopic:nodeid:flags
 * N.B. distopic is the result of running message topic thru disconnect_topic()
 * and flags is either 0 or FLUX_MSGFLAG_UPSTREAM (the only routing flag).
 * Returns number of bytes written to buf (excluding \0) or -1 on error.
 */
int disconnect_hashkey (const flux_msg_t *msg, char *buf, int len)
{
    const char *topic;
    uint32_t nodeid;
    int flags = 0;
    int used, n;

    if (!msg || !buf) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_topic (msg, &topic) < 0
        || flux_msg_get_nodeid (msg, &nodeid) < 0)
        return -1;
    if (flux_msg_has_flag (msg, FLUX_MSGFLAG_UPSTREAM))
        flags = FLUX_MSGFLAG_UPSTREAM;
    if ((used = disconnect_topic (topic, buf, len)) < 0)
        return -1;
    len -= used;
    buf += used;
    n = snprintf (buf, len, ":%lu:%d", (unsigned long)nodeid, flags);
    if (n >= len) {
        errno = EINVAL;
        return -1;
    }
    return used + n;
}

/* Create disconnect message as a clone of 'msg', with new topic string,
 * run through disconnect_topic() above.
 */
flux_msg_t *disconnect_msg (const flux_msg_t *msg)
{
    flux_msg_t *cpy;
    const char *topic;
    char distopic[TOPIC_BUFSIZE];

    if (flux_msg_get_topic (msg, &topic) < 0)
        return NULL;
    if (disconnect_topic (topic, distopic, sizeof (distopic)) < 0)
        return NULL;
    if (!(cpy = flux_msg_copy (msg, false)))
        return NULL;
    if (flux_msg_set_topic (cpy, distopic) < 0)
        goto error;
    if (flux_msg_set_noresponse (cpy) < 0)
        goto error;
    return cpy;
error:
    flux_msg_destroy (cpy);
    return NULL;
}

/* Insert a disconnect message cloned from 'msg' in the disconnect hash,
 * if there isn't already a matching entry.
 */
int disconnect_arm (struct disconnect *dcon, const flux_msg_t *msg)
{
    char key[HASHKEY_BUFSIZE];

    if (flux_msg_is_noresponse (msg))
        return 0;
    if (disconnect_hashkey (msg, key, sizeof (key)) < 0)
        return -1;
    if (!zhashx_lookup (dcon->hash, key)) {
        flux_msg_t *dmsg;
        if (!(dmsg = disconnect_msg (msg)))
            return -1;
        (void)zhashx_insert (dcon->hash, key, dmsg);
    }
    return 0;
}

/* Client is disconnecting - send all disconnect messages and destroy them.
 */
static void disconnect_fire (struct disconnect *dcon)
{
    if (dcon) {
        flux_msg_t *msg;

        msg = zhashx_first (dcon->hash);
        while (msg) {
            if (dcon->cb)
                dcon->cb (msg, dcon->arg);
            msg = zhashx_next (dcon->hash);
        }
        zhashx_purge (dcon->hash);
    }
}

void disconnect_destroy (struct disconnect *dcon)
{
    if (dcon) {
        ERRNO_SAFE_WRAP (disconnect_fire, dcon);
        ERRNO_SAFE_WRAP (zhashx_destroy, &dcon->hash);
        ERRNO_SAFE_WRAP (free, dcon);
    }
}

struct disconnect *disconnect_create (disconnect_send_f cb, void *arg)
{
    struct disconnect *dcon;

    if (!(dcon = calloc (1, sizeof (*dcon))))
        return NULL;
    if (!(dcon->hash = zhashx_new ()))
        goto error;
    zhashx_set_destructor (dcon->hash, hash_destructor);
    dcon->cb = cb;
    dcon->arg = arg;
    return dcon;
error:
    disconnect_destroy (dcon);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
