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
#include <flux/core.h>
#include <src/common/libutil/shortjson.h>
#include "ping.h"

struct ping_context {
    flux_msg_handler_t *w;
};

static void ping_request_cb (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    //struct ping_context *p = arg;
    json_object *inout = NULL;
    const char *json_str;
    char *s = NULL;
    char *route = NULL;
    uint32_t rank;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!(inout = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    if (!(s = flux_msg_get_route_string (msg)))
        goto error;
    if (flux_get_rank (h, &rank) < 0)
        goto error;
    if (asprintf (&route, "%s!%u", s, rank) < 0) {
        errno = ENOMEM;
        goto error;
    }
    Jadd_str (inout, "route", route);
    if (flux_respond (h, msg, 0, Jtostr (inout)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (s);
    free (route);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (s);
    free (route);
}

static void ping_finalize (void *arg)
{
    struct ping_context *p = arg;
    flux_msg_handler_stop (p->w);
    flux_msg_handler_destroy (p->w);
    free (p);
}

int ping_initialize (flux_t *h)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct ping_context *p = calloc (1, sizeof (*p));
    if (!p) {
        errno = ENOMEM;
        goto error;
    }
    match.typemask = FLUX_MSGTYPE_REQUEST;
    match.topic_glob = "cmb.ping";
    if (!(p->w = flux_msg_handler_create (h, match, ping_request_cb, p)))
        goto error;
    flux_msg_handler_start (p->w);
    flux_aux_set (h, "flux::ping", p, ping_finalize);
    return 0;
error:
    if (p) {
        flux_msg_handler_stop (p->w);
        flux_msg_handler_destroy (p->w);
        free (p);
    }
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
