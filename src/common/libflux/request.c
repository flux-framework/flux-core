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
#include "request.h"
#include "message.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"


int flux_response_recv (flux_t h, JSON *respp, char **tagp, bool nb)
{
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_response_recvmsg (h, nb)))
        goto done;
    if (flux_msg_get_errnum (zmsg, &errno) < 0 || errno != 0)
        goto done;
    if (flux_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

static zmsg_t *response_matched_recvmsg (flux_t h, const char *match, bool nb)
{
    zmsg_t *zmsg, *response = NULL;
    zlist_t *nomatch = NULL;

    do {
        if (!(response = flux_response_recvmsg (h, nb)))
            goto done;
        if (!flux_msg_streq_topic (response, match)) {
            if (!nomatch && !(nomatch = zlist_new ()))
                oom ();
            if (zlist_append (nomatch, response) < 0)
                oom ();
            response = NULL;
        }
    } while (!response);
done:
    if (nomatch) {
        while ((zmsg = zlist_pop (nomatch))) {
            if (flux_response_putmsg (h, &zmsg) < 0)
                zmsg_destroy (&zmsg);
        }
        zlist_destroy (&nomatch);
    }
    return response;
}

static int msg_set_payload_json (zmsg_t *zmsg, JSON o)
{
    const char *s = NULL;
    int flags = 0;

    if (o) {
        if (!(s = json_object_to_json_string (o))) {
            errno = EINVAL; /* not really sure if this can happen */
            return -1;
        }
        flags |= FLUX_MSGFLAG_JSON;
    }
    return flux_msg_set_payload (zmsg, flags, (char *)s, strlen (s));
}

int flux_respond (flux_t h, zmsg_t **zmsg, JSON o)
{
    if (flux_msg_set_type (*zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        return -1;
    if (msg_set_payload_json (*zmsg, o) < 0)
        return -1;
    return flux_response_sendmsg (h, zmsg);
}

int flux_respond_errnum (flux_t h, zmsg_t **zmsg, int errnum)
{
    if (flux_msg_set_type (*zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        return -1;
    if (flux_msg_set_payload (*zmsg, 0, NULL, 0) < 0)
        return -1;
    if (flux_msg_set_errnum (*zmsg, errnum) < 0)
        return -1;
    return flux_response_sendmsg (h, zmsg);
}

/* New general request/rpc functions - not yet exposed.
 */

static int flux_vrequestf (flux_t h, uint32_t nodeid, json_object *o,
                           const char *fmt, va_list ap)
{
    char *topic = xvasprintf (fmt, ap);
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (o && msg_set_payload_json (zmsg, o) < 0)
        goto done;
    if (flux_msg_set_nodeid (zmsg, nodeid) < 0)
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    rc = flux_request_sendmsg (h, &zmsg);
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    free (topic);
    return rc;
}

int flux_requestf (flux_t h, uint32_t nodeid, JSON o, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrequestf (h, nodeid, o, fmt, ap);
    va_end (ap);
    return rc;
}

static int flux_vrpcf (flux_t h, uint32_t nodeid, JSON in, JSON *out,
                        const char *fmt, va_list ap)
{
    char *topic = xvasprintf (fmt, ap);
    zmsg_t *zmsg = NULL;
    int rc = -1;

    if (flux_requestf (h, nodeid, in, "%s", topic) < 0)
        goto done;
    if (!(zmsg = response_matched_recvmsg (h, topic, false)))
        goto done;
    if (flux_msg_get_errnum (zmsg, &errno) < 0 || errno != 0)
        goto done;
    if (flux_msg_decode (zmsg, NULL, out) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    free (topic);
    return rc;
}

int flux_rpcf (flux_t h, uint32_t nodeid, JSON in, JSON *out, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrpcf (h, nodeid, in, out, fmt, ap);
    va_end (ap);
    return rc;
}

/* Old request/rpc functions implemented in terms of new.
 */

JSON flux_rank_rpc (flux_t h, int rank, JSON o, const char *fmt, ...)
{
    uint32_t nodeid = rank == -1 ? FLUX_NODEID_ANY : rank;
    va_list ap;
    JSON out;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrpcf (h, nodeid, o, &out, fmt, ap);
    va_end (ap);
    return rc < 0 ? NULL : out;
}

int flux_rank_request_send (flux_t h, int rank, JSON o, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrequestf (h, rank == -1 ? FLUX_NODEID_ANY : rank, o, fmt, ap);
    va_end (ap);
    return rc;
}

JSON flux_rpc (flux_t h, JSON o, const char *fmt, ...)
{
    va_list ap;
    JSON out;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrpcf (h, FLUX_NODEID_ANY, o, &out, fmt, ap);
    va_end (ap);
    return rc < 0 ? NULL : out;
}

int flux_request_send (flux_t h, JSON o, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vrequestf (h, FLUX_NODEID_ANY, o, fmt, ap);
    va_end (ap);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
