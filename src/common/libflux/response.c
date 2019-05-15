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
#include <stdarg.h>
#include <flux/core.h>

static int response_decode (const flux_msg_t *msg, const char **topic)
{
    int type;
    const char *ts;
    int errnum = 0;
    int rc = -1;

    if (msg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_RESPONSE) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_errnum (msg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
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

int flux_response_decode (const flux_msg_t *msg, const char **topicp, const char **sp)
{
    const char *topic, *s;
    int rc = -1;

    if (response_decode (msg, &topic) < 0)
        goto done;
    if (sp) {
        if (flux_msg_get_string (msg, &s) < 0)
            goto done;
        *sp = s;
    }
    if (topicp)
        *topicp = topic;
    rc = 0;
done:
    return rc;
}

int flux_response_decode_raw (const flux_msg_t *msg,
                              const char **topic,
                              const void **data,
                              int *len)
{
    const char *ts;
    const void *d = NULL;
    int l = 0;
    int rc = -1;

    if (!data || !len) {
        errno = EINVAL;
        goto done;
    }
    if (response_decode (msg, &ts) < 0)
        goto done;
    if (flux_msg_get_payload (msg, &d, &l) < 0) {
        if (errno != EPROTO)
            goto done;
        errno = 0;
    }
    if (topic)
        *topic = ts;
    *data = d;
    *len = l;
    rc = 0;
done:
    return rc;
}

int flux_response_decode_error (const flux_msg_t *msg, const char **errstr)
{
    int type;
    int errnum;
    const char *s = NULL;

    if (!msg || !errstr) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        return -1;
    if (type != FLUX_MSGTYPE_RESPONSE) {
        errno = EPROTO;
        return -1;
    }
    if (flux_msg_get_errnum (msg, &errnum) < 0)
        return -1;
    if (errnum == 0) {
        errno = ENOENT;
        return -1;
    }
    if (flux_msg_get_string (msg, &s) < 0)
        return -1;
    if (s == NULL) {
        errno = ENOENT;
        return -1;
    }
    *errstr = s;
    return 0;
}

static flux_msg_t *response_encode (const char *topic, int errnum)
{
    flux_msg_t *msg = NULL;

    if (!topic) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)))
        goto error;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto error;
    if (flux_msg_enable_route (msg) < 0)
        goto error;
    if (flux_msg_set_errnum (msg, errnum) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_response_encode (const char *topic, const char *s)
{
    flux_msg_t *msg;

    if (!(msg = response_encode (topic, 0)))
        goto error;
    if (s && flux_msg_set_string (msg, s) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_response_encode_raw (const char *topic, const void *data, int len)
{
    flux_msg_t *msg;

    if (!(msg = response_encode (topic, 0)))
        goto error;
    if (data && flux_msg_set_payload (msg, data, len) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_response_encode_error (const char *topic,
                                        int errnum,
                                        const char *errstr)
{
    flux_msg_t *msg;

    if (errnum == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(msg = response_encode (topic, errnum)))
        goto error;
    if (errstr && flux_msg_set_string (msg, errstr) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

static flux_msg_t *derive_response (const flux_msg_t *request, int errnum)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_copy (request, false)))
        return NULL;
    if (flux_msg_set_type (msg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto error;
    if (flux_msg_set_userid (msg, FLUX_USERID_UNKNOWN) < 0)
        goto error;
    if (flux_msg_set_rolemask (msg, FLUX_ROLE_NONE) < 0)
        goto error;
    if (errnum && flux_msg_set_errnum (msg, errnum) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int flux_respond (flux_t *h, const flux_msg_t *request, const char *s)
{
    flux_msg_t *msg = NULL;

    if (!h || !request)
        goto inval;
    msg = derive_response (request, 0);
    if (!msg)
        goto error;
    if (s && flux_msg_set_string (msg, s) < 0)
        goto error;
    if (flux_send (h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
inval:
    errno = EINVAL;
error:
    flux_msg_destroy (msg);
    return -1;
}

static int flux_respond_vpack (flux_t *h,
                               const flux_msg_t *request,
                               const char *fmt,
                               va_list ap)
{
    flux_msg_t *msg = NULL;

    if (!h || !request || !fmt)
        goto inval;
    msg = derive_response (request, 0);
    if (!msg)
        goto error;
    if (flux_msg_vpack (msg, fmt, ap) < 0)
        goto error;
    if (flux_send (h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
inval:
    errno = EINVAL;
error:
    flux_msg_destroy (msg);
    return -1;
}

int flux_respond_pack (flux_t *h, const flux_msg_t *request, const char *fmt, ...)
{
    int rc;
    va_list ap;

    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    rc = flux_respond_vpack (h, request, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_respond_raw (flux_t *h, const flux_msg_t *request, const void *data, int len)
{
    flux_msg_t *msg = NULL;

    if (!h || !request)
        goto inval;
    msg = derive_response (request, 0);
    if (!msg)
        goto error;
    if (data && flux_msg_set_payload (msg, data, len) < 0)
        goto error;
    if (flux_send (h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
inval:
    errno = EINVAL;
error:
    flux_msg_destroy (msg);
    return -1;
}

int flux_respond_error (flux_t *h,
                        const flux_msg_t *request,
                        int errnum,
                        const char *errstr)
{
    flux_msg_t *msg = NULL;

    if (!h || !request || errnum == 0)
        goto inval;
    msg = derive_response (request, errnum);
    if (!msg)
        goto error;
    if (errstr) {
        if (flux_msg_set_string (msg, errstr) < 0)
            goto error;
    }
    if (flux_send (h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
inval:
    errno = EINVAL;
error:
    flux_msg_destroy (msg);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
