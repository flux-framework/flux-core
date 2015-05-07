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

#include "src/common/libutil/log.h"
#include "src/common/libmrpc/mrpc.h"

/* Copy input arguments to output arguments and respond to RPC.
 */
static int mecho_mrpc_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    json_object *request = NULL;
    json_object *inarg = NULL;
    flux_mrpc_t f = NULL;

    if (flux_json_event_decode (*zmsg, &request) < 0) {
        flux_log (h, LOG_ERR, "flux_json_event_decode: %s", strerror (errno));
        goto done;
    }
    if (!request) {
        flux_log (h, LOG_ERR, "missing JSON part");
        goto done;
    }
    if (!(f = flux_mrpc_create_fromevent (h, request))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "flux_mrpc_create_fromevent: %s",
                                    strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (f, &inarg) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_get_inarg: %s", strerror (errno));
        goto done;
    }
    flux_mrpc_put_outarg (f, inarg);
    if (flux_mrpc_respond (f) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    if (request)
        json_object_put (request);
    if (inarg)
        json_object_put (inarg);
    if (f)
        flux_mrpc_destroy (f);
    zmsg_destroy (zmsg);
    return 0;
}

int mod_main (flux_t h, zhash_t *args)
{
    if (flux_event_subscribe (h, "mrpc.mecho") < 0) {
        flux_log (h, LOG_ERR, "%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }
    if (flux_msghandler_add (h, FLUX_MSGTYPE_EVENT, "mrpc.mecho",
                                                    mecho_mrpc_cb, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("mecho");

/*
 * vi: ts=4 sw=4 expandtab
 */
