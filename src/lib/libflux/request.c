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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "shortjson.h"
#include "zdump.h"
#include "jsonutil.h"
#include "xzmalloc.h"

#include "flux.h"

int flux_request_send (flux_t h, json_object *request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;
    json_object *empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = flux_msg_encode (tag, request);
    free (tag);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if ((rc = flux_request_sendmsg (h, &zmsg)) < 0)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    return rc;
}

int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb)
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

json_object *flux_rpc (flux_t h, json_object *request, const char *fmt, ...)
{
    char *tag = NULL;
    json_object *response = NULL;
    zmsg_t *zmsg = NULL;
    va_list ap;
    json_object *empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = flux_msg_encode (tag, request);

    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if (flux_request_sendmsg (h, &zmsg) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, tag, false)))
        goto done;
    if (flux_msg_decode (zmsg, NULL, &response) < 0 || !response)
        goto done;
    if (util_json_object_get_int (response, "errnum", &errno) == 0) {
        json_object_put (response);
        response = NULL;
        goto done;
    }
done:
    if (tag)
        free (tag);
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    return response;
}

int flux_respond (flux_t h, zmsg_t **reqmsg, json_object *response)
{
    if (flux_msg_replace_json (*reqmsg, response) < 0)
        return -1;
    return flux_response_sendmsg (h, reqmsg);
}

int flux_respond_errnum (flux_t h, zmsg_t **reqmsg, int errnum)
{
    if (flux_msg_replace_json_errnum (*reqmsg, errnum) < 0)
        return -1;
    return flux_response_sendmsg (h, reqmsg);
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
    JSON empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = Jnew ();
    zmsg = flux_msg_encode (tag, request);

    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if (flux_rank_request_sendmsg (h, rank, &zmsg) < 0)
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
    Jput (empty);
    return response;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
