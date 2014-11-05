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


int flux_request_send (flux_t h, JSON request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc = -1;
    va_list ap;
    JSON empty = NULL;

    va_start (ap, fmt);
    tag = xvasprintf (fmt, ap);
    va_end (ap);

    if (!request)
        request = empty = Jnew ();
    if (!(zmsg = flux_msg_encode (tag, request)))
        goto done;
    if (flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST) < 0)
        goto done;
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        oom ();
    rc = flux_request_sendmsg (h, &zmsg);
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    Jput (empty);
    free (tag);
    return rc;
}

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

JSON flux_rpc (flux_t h, JSON request, const char *fmt, ...)
{
    char *tag = NULL;
    JSON response = NULL;
    va_list ap;
    zmsg_t *zmsg;

    va_start (ap, fmt);
    tag = xvasprintf (fmt, ap);
    va_end (ap);

    if (flux_request_send (h, request, "%s", tag) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, tag, false)))
        goto done;
    if (flux_msg_decode (zmsg, NULL, &response) < 0 || !response)
        goto done;
    if (Jget_int (response, "errnum", &errno)) {
        Jput (response);
        response = NULL;
        goto done;
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);
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

static int flux_rank_fwd (flux_t h, int rank, const char *topic, JSON payload)
{
    JSON request = Jnew ();
    int ret = -1;

    Jadd_int (request, "rank", rank);
    Jadd_str (request, "topic", topic);
    Jadd_obj (request, "payload", payload);
    if (flux_request_send (h, request, "cmb.rankfwd") < 0)
        goto done;
    ret = 0;
done:
    Jput (request);
    return ret;
}

int flux_rank_request_sendmsg (flux_t h, int rank, zmsg_t **zmsg)
{
    char *topic = NULL;
    JSON payload = NULL;
    int rc = -1;

    if (rank == -1) {
        rc = flux_request_sendmsg (h, zmsg);
        goto done;
    }

    if (!*zmsg || flux_msg_decode (*zmsg, &topic, &payload) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (flux_rank_fwd (h, rank, topic, payload) < 0)
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

int flux_rank_request_send (flux_t h, int rank, JSON request,
                            const char *fmt, ...)
{
    char *topic;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&topic, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (rank == -1)
        rc = flux_request_send (h, request, "%s", topic);
    else
        rc = flux_rank_fwd (h, rank, topic, request);
    free (topic);
    return rc;
}

JSON flux_rank_rpc (flux_t h, int rank, JSON request, const char *fmt, ...)
{
    char *tag = NULL;
    JSON response = NULL;
    zmsg_t *zmsg = NULL;
    va_list ap;

    va_start (ap, fmt);
    tag = xvasprintf (fmt, ap);
    va_end (ap);

    if (flux_rank_request_send (h, rank, request, "%s", tag) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, tag, false)))
        goto done;
    if (flux_msg_decode (zmsg, NULL, &response) < 0 || !response)
        goto done;
    if (Jget_int (response, "errnum", &errno)) {
        Jput (response);
        response = NULL;
        goto done;
    }
done:
    if (tag)
        free (tag);
    if (zmsg)
        zmsg_destroy (&zmsg);
    return response;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
