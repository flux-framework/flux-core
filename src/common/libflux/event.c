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
#include <stdbool.h>

#include "event.h"
#include "message.h"

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


int flux_event_decode (const flux_msg_t *msg, const char **topic, const char **json_str)
{
    const char *ts, *js;
    int rc = -1;

    if (event_decode (msg, &ts) < 0)
        goto done;
    if (flux_msg_get_json (msg, &js) < 0)
        goto done;
    if (topic)
        *topic = ts;
    if (json_str)
        *json_str = js;
    rc = 0;
done:
    return rc;
}

static int flux_event_vdecodef (const flux_msg_t *msg, const char **topic,
                                const char *fmt, va_list ap)
{
    const char *ts;
    int rc = -1;

    if (event_decode (msg, &ts) < 0)
        goto done;
    if (flux_msg_vget_jsonf (msg, fmt, ap) < 0)
        goto done;
    if (topic)
        *topic = ts;
    rc = 0;
done:
    return rc;
}

int flux_event_decodef (const flux_msg_t *msg, const char **topic,
                        const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_event_vdecodef (msg, topic, fmt, ap);
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
    if (flux_msg_enable_route (msg) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *flux_event_encode (const char *topic, const char *json_str)
{
    flux_msg_t *msg = flux_event_create (topic);
    if (!msg)
        goto error;
    if (json_str && flux_msg_set_json (msg, json_str) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

static flux_msg_t *flux_event_vencodef (const char *topic,
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

flux_msg_t *flux_event_encodef (const char *topic, const char *fmt, ...)
{
    flux_msg_t *msg;
    va_list ap;

    va_start (ap, fmt);
    msg = flux_event_vencodef (topic, fmt, ap);
    va_end (ap);
    return msg;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
