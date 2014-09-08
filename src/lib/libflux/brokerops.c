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

/* cmbdcli.c - client code for built-in cmbd queries */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "xzmalloc.h"
#include "shortjson.h"

#include "flux.h"

char *flux_getattr (flux_t h, int rank, const char *name)
{
    JSON request = Jnew ();
    JSON response = NULL;
    char *ret = NULL;
    const char *val;

    Jadd_str (request, "name", name);
    if (!(response = flux_rank_rpc (h, rank, request, "cmb.getattr")))
        goto done;
    if (!Jget_str (response, (char *)name, &val)) {
        errno = EPROTO;
        goto done;
    }
    ret = xstrdup (val);
done:
    Jput (request);
    Jput (response);
    return ret;
}

int flux_info (flux_t h, int *rankp, int *sizep, bool *treerootp)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rank, size;
    bool treeroot;
    int ret = -1;

    if (!(response = flux_rpc (h, request, "cmb.info")))
        goto done;
    if (!Jget_bool (response, "treeroot", &treeroot)
            || !Jget_int (response, "rank", &rank)
            || !Jget_int (response, "size", &size)) {
        errno = EPROTO;
        goto done;
    }
    if (rankp)
        *rankp = rank;
    if (sizep)
        *sizep = size;
    if (treerootp)
        *treerootp = treeroot;
    ret = 0;
done:
    Jput (request);
    Jput (response);
    return ret;
}

int flux_size (flux_t h)
{
    int size = -1;
    flux_info (h, NULL, &size, NULL);
    return size;
}

bool flux_treeroot (flux_t h)
{
    bool treeroot = false;
    flux_info (h, NULL, NULL, &treeroot);
    return treeroot;
}

int flux_rmmod (flux_t h, int rank, const char *name, int flags)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "name", name);
    Jadd_int (request, "flags", flags);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.rmmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

JSON flux_lsmod (flux_t h, int rank)
{
    JSON request = Jnew ();
    JSON response = NULL;

    response = flux_rank_rpc (h, rank, request, "cmb.lsmod");
    Jput (request);
    return response;
}

int flux_insmod (flux_t h, int rank, const char *path, int flags, JSON args)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "path", path);
    Jadd_int (request, "flags", flags);
    Jadd_obj (request, "args", args);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.insmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

JSON flux_lspeer (flux_t h, int rank)
{
    JSON request = Jnew ();
    JSON response = NULL;

    response = flux_rank_rpc (h, rank, request, "cmb.lspeer");
    Jput (request);
    return response;
}

int flux_reparent (flux_t h, int rank, const char *uri)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    if (!uri) {
        errno = EINVAL;
        goto done;
    }
    Jadd_str (request, "uri", uri);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.reparent"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

int flux_panic (flux_t h, int rank, const char *msg)
{
    JSON request = Jnew ();
    int rc = -1;

    if (msg)
        Jadd_str (request, "msg", msg);
    if (flux_rank_request_send (h, rank, request, "cmb.panic") < 0)
        goto done;
    /* No reply */
    rc = 0;
done:
    Jput (request);
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

/* Emulate former flux_t handle operations.
 */

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
