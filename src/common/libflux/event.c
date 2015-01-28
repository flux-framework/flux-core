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
#include "rpc.h"

#include "src/common/libutil/shortjson.h"

int flux_json_event_decode (zmsg_t *zmsg, json_object **in)
{
    int type;
    int rc = -1;
    JSON o;

    if (in == NULL || zmsg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_EVENT) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_payload_json (zmsg, &o) < 0)
        goto done;
    if (o == NULL) {
        errno = EPROTO;
        goto done;
    }
    *in = o;
    rc = 0;
done:
    return rc;
}

int flux_event_recv (flux_t h, json_object **respp, char **tagp, bool nb)
{
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 0,
        .topic_glob = NULL,
    };
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_recvmsg_match (h, match, NULL, nb)))
        goto done;
    if (flux_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

int flux_event_pub (flux_t h, const char *topic, JSON payload)
{
    JSON request = Jnew ();
    JSON response = NULL;
    JSON empty_payload = NULL;
    int ret = -1;

    Jadd_str (request, "topic", topic);
    if (!payload)
        payload = empty_payload = Jnew ();
    Jadd_obj (request, "payload", payload);
    errno = 0;
    response = flux_rpc (h, request, "cmb.pub");
    if (response) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    ret = 0;
done:
    Jput (request);
    Jput (response);
    Jput (empty_payload);
    return ret;
}

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg)
{
    char *topic = NULL;
    JSON payload = NULL;
    int rc = -1;

    if (!*zmsg || flux_msg_decode (*zmsg, &topic, &payload) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (flux_event_pub (h, topic, payload) < 0)
        goto done;
    if (*zmsg)
        zmsg_destroy (zmsg);
    rc = 0;
done:
    if (topic)
        free (topic);
    Jput (payload);
    return rc;
}

int flux_event_send (flux_t h, JSON request, const char *fmt, ...)
{
    char *topic;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&topic, fmt, ap) < 0)
        oom ();
    va_end (ap);

    rc = flux_event_pub (h, topic, request);
    free (topic);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
