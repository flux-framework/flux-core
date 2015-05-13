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
#include "src/common/libutil/xzmalloc.h"

int flux_event_decode (zmsg_t *zmsg, const char **topic, const char **json_str)
{
    int type;
    const char *ts, *js;
    int rc = -1;

    if (zmsg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_EVENT) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_topic (zmsg, &ts) < 0)
        goto done;
    if (flux_msg_get_payload_json_str (zmsg, &js) < 0)
        goto done;
    if ((json_str && !js) || (!json_str && js)) {
        errno = EPROTO;
        goto done;
    }
    if (topic)
        *topic = ts;
    if (json_str)
        *json_str = js;
    rc = 0;
done:
    return rc;
}

zmsg_t *flux_event_encode (const char *topic, const char *json_str)
{
    zmsg_t *zmsg = NULL;

    if (!topic) {
        errno = EINVAL;
        goto error;
    }
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        goto error;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto error;
    if (flux_msg_enable_route (zmsg) < 0)
        goto error;
    if (json_str && flux_msg_set_payload_json_str (zmsg, json_str) < 0)
        goto error;
    return zmsg;
error:
    if (zmsg) {
        int saved_errno = errno;
        zmsg_destroy (&zmsg);
        errno = saved_errno;
    }
    return NULL;
}

int flux_event_recv (flux_t h, json_object **in, char **topic, bool nb)
{
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 0,
        .topic_glob = NULL,
    };
    zmsg_t *zmsg;
    const char *s;
    int rc = -1;

    if (!(zmsg = flux_recvmsg_match (h, match, NULL, nb)))
        goto done;
    if (flux_msg_get_topic (zmsg, &s) < 0)
        goto done;
    if (flux_msg_get_payload_json (zmsg, in) < 0)
        goto done;
    *topic = xstrdup (s);
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

int flux_event_send (flux_t h, JSON in, const char *fmt, ...)
{
    zmsg_t *zmsg = NULL;
    char *topic = NULL;
    va_list ap;
    int rc = -1;
    JSON empty = NULL;

    va_start (ap, fmt);
    topic = xvasprintf (fmt, ap);
    va_end (ap);

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;

    /* FIXME: old flux_event_send () always sent an empty event and
     * t/lua/t0003-events.t (tests 19 and 20) will fail if this isn't so.
     * Need to run down whether this can safely change.
     */
    if (!in)
        in = empty = Jnew ();

    if (flux_msg_set_payload_json (zmsg, in) < 0)
        goto done;
    rc = flux_sendmsg (h, &zmsg);
done:
    zmsg_destroy (&zmsg);
    if (topic)
        free (topic);
    if (empty)
        Jput (empty);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
