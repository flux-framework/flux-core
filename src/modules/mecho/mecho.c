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
#include "src/common/libutil/shortjson.h"
#include "src/modules/libmrpc/mrpc.h"
#include "src/modules/libmrpc/mrpc_deprecated.h"

/* Copy input arguments to output arguments and respond to RPC.
 */
static void mecho_mrpc_cb (flux_t h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    json_object *request = NULL;
    json_object *inarg = NULL;
    flux_mrpc_t *f = NULL;

    if (flux_event_decode (msg, NULL, &json_str) < 0
                || !(request = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "flux_event_decode: %s", strerror (errno));
        goto done;
    }
    if (!(f = flux_mrpc_create_fromevent_obj (h, request))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "flux_mrpc_create_fromevent: %s",
                                    strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg_obj (f, &inarg) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_get_inarg: %s", strerror (errno));
        goto done;
    }
    flux_mrpc_put_outarg_obj (f, inarg);
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
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,   "mrpc.mecho",       mecho_mrpc_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t h, int argc, char **argv)
{
    int rc = -1;
    if (flux_event_subscribe (h, "mrpc.mecho") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, NULL) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }
    rc = 0;
done_unreg:
    flux_msg_handler_delvec (htab);
done:
    return rc;
}

MOD_NAME ("mecho");

/*
 * vi: ts=4 sw=4 expandtab
 */
