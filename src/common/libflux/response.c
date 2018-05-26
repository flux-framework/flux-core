/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdarg.h>

#include "response.h"
#include "message.h"

#include "src/common/libutil/nodeset.h"

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

int flux_response_decode (const flux_msg_t *msg, const char **topicp,
                          const char **sp)
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

int flux_response_decode_raw (const flux_msg_t *msg, const char **topic,
                              const void **data, int *len)
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

flux_msg_t *flux_response_encode (const char *topic, int errnum, const char *s)
{
    flux_msg_t *msg;

    if (!(msg = response_encode (topic, errnum)))
        goto error;
    if ((errnum != 0 && s != NULL)) {
        errno = EINVAL;
        goto error;
    }
    if (s && flux_msg_set_string (msg, s) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_response_encode_raw (const char *topic, int errnum,
                                      const void *data, int len)
{
    flux_msg_t *msg;

    if (!(msg = response_encode (topic, errnum)))
        goto error;
    if ((errnum != 0 && data != NULL)) {
        errno = EINVAL;
        goto error;
    }
    if (data && flux_msg_set_payload (msg, data, len) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

static flux_msg_t *derive_response (flux_t *h, const flux_msg_t *request,
                                    int errnum)
{
    flux_msg_t *msg = NULL;

    if (!request) {
        errno = EINVAL;
        goto fatal;
    }
    if (!(msg = flux_msg_copy (request, false)))
        goto fatal;
    if (flux_msg_set_type (msg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto fatal;
    if (flux_msg_set_userid (msg, FLUX_USERID_UNKNOWN) < 0)
        goto fatal;
    if (flux_msg_set_rolemask (msg, FLUX_ROLE_NONE) < 0)
        goto fatal;
    if (errnum && flux_msg_set_errnum (msg, errnum) < 0)
        goto fatal;
    return msg;
fatal:
    flux_msg_destroy (msg);
    FLUX_FATAL (h);
    return NULL;
}

int flux_respond (flux_t *h, const flux_msg_t *request,
                  int errnum, const char *s)
{
    flux_msg_t *msg = derive_response (h, request, errnum);
    if (!msg)
        goto fatal;
    if (!errnum && s&& flux_msg_set_string (msg, s) < 0)
        goto fatal;
    if (flux_send (h, msg, 0) < 0)
        goto fatal;
    flux_msg_destroy (msg);
    return 0;
fatal:
    flux_msg_destroy (msg);
    FLUX_FATAL (h);
    return -1;
}

static int flux_respond_vpack (flux_t *h, const flux_msg_t *request,
                               const char *fmt, va_list ap)
{
    flux_msg_t *msg = derive_response (h, request, 0);
    if (!msg)
        goto fatal;
    if (flux_msg_vpack (msg, fmt, ap) < 0)
        goto fatal;
    if (flux_send (h, msg, 0) < 0)
        goto fatal;
    flux_msg_destroy (msg);
    return 0;
fatal:
    flux_msg_destroy (msg);
    FLUX_FATAL (h);
    return -1;
}

int flux_respond_pack (flux_t *h, const flux_msg_t *request,
                       const char *fmt, ...)
{
    int rc;
    va_list ap;

    va_start (ap, fmt);
    rc = flux_respond_vpack (h, request, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_respond_raw (flux_t *h, const flux_msg_t *request,
                      int errnum, const void *data, int len)
{
    flux_msg_t *msg = derive_response (h, request, errnum);
    if (!msg)
        goto fatal;
    if (!errnum && data && flux_msg_set_payload (msg, data, len) < 0)
        goto fatal;
    if (flux_send (h, msg, 0) < 0)
        goto fatal;
    flux_msg_destroy (msg);
    return 0;
fatal:
    flux_msg_destroy (msg);
    FLUX_FATAL (h);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
