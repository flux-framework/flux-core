/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdarg.h>
#include <jansson.h>

#include "ccan/base64/base64.h"

#include "event.h"
#include "rpc.h"
#include "message.h"

flux_future_t *flux_event_subscribe_ex (flux_t *h,
                                        const char *topic,
                                        int flags)
{
    if (!topic) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "event.subscribe",
                          FLUX_NODEID_ANY,
                          flags,
                          "{s:s}",
                          "topic", topic);
}

int flux_event_subscribe (flux_t *h, const char *topic)
{
    flux_future_t *f;

    if (h && (flux_flags_get (h) & FLUX_O_TEST_NOSUB))
        return 0;
    if (!(f = flux_event_subscribe_ex (h, topic, 0))
        || flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

flux_future_t *flux_event_unsubscribe_ex (flux_t *h,
                                          const char *topic,
                                          int flags)
{
    if (!topic) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "event.unsubscribe",
                          FLUX_NODEID_ANY,
                          flags,
                          "{s:s}",
                          "topic", topic);
}

int flux_event_unsubscribe (flux_t *h, const char *topic)
{
    flux_future_t *f;

    if (h && (flux_flags_get (h) & FLUX_O_TEST_NOSUB))
        return 0;
    if (!(f = flux_event_unsubscribe_ex (h, topic, 0))
        || flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static int event_decode (const flux_msg_t *msg, const char **topic)
{
    int type;
    const char *ts;
    int rc = -1;

    if (msg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_EVENT) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_topic (msg, &ts) < 0)
        goto done;
    if (topic)
        *topic = ts;
    rc = 0;
done:
    return rc;
}


int flux_event_decode (const flux_msg_t *msg, const char **topicp,
                       const char **sp)
{
    const char *topic, *s;
    int rc = -1;

    if (event_decode (msg, &topic) < 0)
        goto done;
    if (flux_msg_get_string (msg, &s) < 0)
        goto done;
    if (topicp)
        *topicp = topic;
    if (sp)
        *sp = s;
    rc = 0;
done:
    return rc;
}

int flux_event_decode_raw (const flux_msg_t *msg, const char **topicp,
                           const void **datap, int *lenp)
{
    const char *topic;
    const void *data = NULL;
    int len = 0;
    int rc = -1;

    if (!datap || !lenp) {
        errno = EINVAL;
        goto done;
    }
    if (event_decode (msg, &topic) < 0)
        goto done;
    if (flux_msg_get_payload (msg, &data, &len) < 0) {
        if (errno != EPROTO)
            goto done;
        errno = 0;
    }
    if (topicp)
        *topicp = topic;
    *datap = data;
    *lenp = len;
    rc = 0;
done:
    return rc;
}

static int flux_event_vunpack (const flux_msg_t *msg, const char **topic,
                               const char *fmt, va_list ap)
{
    const char *ts;
    int rc = -1;

    if (event_decode (msg, &ts) < 0)
        goto done;
    if (flux_msg_vunpack (msg, fmt, ap) < 0)
        goto done;
    if (topic)
        *topic = ts;
    rc = 0;
done:
    return rc;
}

int flux_event_unpack (const flux_msg_t *msg, const char **topic,
                       const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_event_vunpack (msg, topic, fmt, ap);
    va_end (ap);
    return rc;
}

static flux_msg_t *flux_event_create (const char *topic)
{
    flux_msg_t *msg = NULL;

    if (!topic) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        goto error;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto error;
    flux_msg_route_enable (msg);
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_event_encode (const char *topic, const char *s)
{
    flux_msg_t *msg = flux_event_create (topic);
    if (!msg)
        goto error;
    if (s && flux_msg_set_string (msg, s) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_event_encode_raw (const char *topic,
                                   const void *data, int len)
{
    flux_msg_t *msg = flux_event_create (topic);
    if (!msg)
        goto error;
    if (data && flux_msg_set_payload (msg, data, len) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

static flux_msg_t *flux_event_vpack (const char *topic,
                                     const char *fmt, va_list ap)
{
    flux_msg_t *msg = flux_event_create (topic);
    if (!msg)
        goto error;
    if (flux_msg_vpack (msg, fmt, ap) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_event_pack (const char *topic, const char *fmt, ...)
{
    flux_msg_t *msg;
    va_list ap;

    va_start (ap, fmt);
    msg = flux_event_vpack (topic, fmt, ap);
    va_end (ap);
    return msg;
}

static flux_future_t *wrap_event_rpc (flux_t *h,
                                      const char *topic, int flags,
                                      const void *src, int srclen)
{
    flux_future_t *f;

    if (src) {
        size_t dstbuflen = base64_encoded_length (srclen) + 1; /* +1 for NUL */
        char *dst;
        if (!(dst = malloc (dstbuflen)))
            return NULL;
        if (base64_encode (dst, dstbuflen, (const char *)src, srclen) < 0) {
            free (dst);
            errno = EPROTO;
            return NULL;
        }
        if (!(f = flux_rpc_pack (h, "event.publish", 0, 0,
                                 "{s:s s:i s:s}", "topic", topic,
                                                  "flags", flags,
                                                  "payload", dst))) {
            int saved_errno = errno;
            free (dst);
            errno = saved_errno;
            return NULL;
        }
        free (dst);
    }
    else {
        if (!(f = flux_rpc_pack (h, "event.publish", 0, 0,
                                    "{s:s s:i}", "topic", topic,
                                                 "flags", flags))) {
            return NULL;
        }
    }
    return f;
}

flux_future_t *flux_event_publish (flux_t *h,
                                   const char *topic, int flags,
                                   const char *json_str)
{
    int len = 0;
    if (!h || !topic || (flags & ~(FLUX_MSGFLAG_PRIVATE)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (json_str)
        len = strlen (json_str) + 1;
    return wrap_event_rpc (h, topic, flags, json_str, len);
}

flux_future_t *flux_event_publish_pack (flux_t *h,
                                        const char *topic, int flags,
                                        const char *fmt, ...)
{
    va_list ap;
    json_t *o;
    char *json_str;
    flux_future_t *f;

    if (!h || !topic || !fmt || (flags & ~(FLUX_MSGFLAG_PRIVATE)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    va_start (ap, fmt);
    o = json_vpack_ex (NULL, 0, fmt, ap);
    va_end (ap);
    if (!o) {
        errno = EINVAL;
        return NULL;
    }
    if (!(json_str = json_dumps (o, JSON_COMPACT))) {
        json_decref (o);
        errno = ENOMEM;
        return NULL;
    }
    json_decref (o);
    if (!(f = wrap_event_rpc (h,  topic, flags,
                              json_str, strlen (json_str) + 1))) {
        int saved_errno = errno;
        free (json_str);
        errno = saved_errno;
        return NULL;
    }
    free (json_str);
    return f;
}

flux_future_t *flux_event_publish_raw (flux_t *h,
                                       const char *topic, int flags,
                                       const void *data, int len)
{
    if (!h || !topic || (flags & ~(FLUX_MSGFLAG_PRIVATE)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    return wrap_event_rpc (h, topic, flags, data, len);
}

int flux_event_publish_get_seq (flux_future_t *f, int *seq)
{
    return flux_rpc_get_unpack (f, "{s:i}", "seq", seq);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
