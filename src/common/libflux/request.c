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
    if (flux_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

zmsg_t *flux_response_matched_recvmsg (flux_t h, const char *match, bool nb)
{
    zmsg_t *zmsg, *response = NULL;
    zlist_t *nomatch;

    if (!(nomatch = zlist_new ()))
        oom ();
    do {
        if (!(response = flux_response_recvmsg (h, nb)))
            goto done;
        if (!flux_msg_match (response, match)) {
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

int flux_respond (flux_t h, zmsg_t **zmsg, JSON o)
{
    if (flux_msg_replace_json (*zmsg, o) < 0)
        return -1;
    if (flux_msg_set_type (*zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        return -1;
    return flux_response_sendmsg (h, zmsg);
}

int flux_respond_errnum (flux_t h, zmsg_t **zmsg, int errnum)
{
    if (flux_msg_replace_json_errnum (*zmsg, errnum) < 0)
        return -1;
    if (flux_msg_set_type (*zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
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
    JSON empty = NULL;

    if (!o)
        o = empty = Jnew ();
    if (!(zmsg = flux_msg_encode (topic, o)))
        goto done;
    if (flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST) < 0)
        goto done;
    if (flux_msg_set_nodeid (zmsg, nodeid) < 0)
        goto done;
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        oom ();
    rc = flux_request_sendmsg (h, &zmsg);
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    Jput (empty);
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

static JSON flux_vrpcf (flux_t h, uint32_t nodeid, JSON o,
                        const char *fmt, va_list ap)
{
    char *topic = xvasprintf (fmt, ap);
    JSON r = NULL;
    zmsg_t *zmsg = NULL;

    if (flux_requestf (h, nodeid, o, "%s", topic) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, topic, false)))
        goto done;
    if (flux_msg_decode (zmsg, NULL, &r) < 0 || r == NULL)
        goto done;
    if (Jget_int (r, "errnum", &errno)) {
        Jput (r);
        r = NULL;
        goto done;
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    free (topic);
    return r;
}

JSON flux_rpcf (flux_t h, uint32_t nodeid, JSON o, const char *fmt, ...)
{
    va_list ap;
    JSON r;

    va_start (ap, fmt);
    r = flux_vrpcf (h, nodeid, o, fmt, ap);
    va_end (ap);
    return r;
}

/* Old request/rpc functions implemented in terms of new.
 */

JSON flux_rank_rpc (flux_t h, int rank, JSON o, const char *fmt, ...)
{
    va_list ap;
    JSON r;

    va_start (ap, fmt);
    r = flux_vrpcf (h, rank == -1 ? FLUX_NODEID_ANY : rank, o, fmt, ap);
    va_end (ap);
    return r;
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
    JSON r;

    va_start (ap, fmt);
    r = flux_vrpcf (h, FLUX_NODEID_ANY, o, fmt, ap);
    va_end (ap);
    return r;
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
