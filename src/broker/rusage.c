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
#include <src/common/libutil/getrusage_json.h>
#include <src/common/libutil/shortjson.h>
#include "rusage.h"

struct rusage_context {
    flux_msg_handler_t *w;
};


static void rusage_request_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    json_object *out = NULL;

    if (getrusage_json (RUSAGE_THREAD, &out) < 0)
        goto error;
    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (out);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void rusage_finalize (void *arg)
{
    struct rusage_context *r = arg;
    flux_msg_handler_stop (r->w);
    flux_msg_handler_destroy (r->w);
    free (r);
}

int rusage_initialize (flux_t *h)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct rusage_context *r = calloc (1, sizeof (*r));
    if (!r) {
        errno = ENOMEM;
        goto error;
    }
    match.typemask = FLUX_MSGTYPE_REQUEST;
    match.topic_glob = "cmb.rusage";
    if (!(r->w = flux_msg_handler_create (h, match, rusage_request_cb, r)))
        goto error;
    flux_msg_handler_start (r->w);
    flux_aux_set (h, "flux::rusage", r, rusage_finalize);
    return 0;
error:
    if (r) {
        flux_msg_handler_stop (r->w);
        flux_msg_handler_destroy (r->w);
        free (r);
    }
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
