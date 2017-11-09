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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif
#include <jansson.h>
#include <czmq.h>

#include "request.h"
#include "response.h"
#include "message.h"
#include "info.h"
#include "rpc.h"
#include "reactor.h"
#include "msg_handler.h"

#include "src/common/libutil/nodeset.h"

struct flux_rpc {
    flux_t *h;
    uint32_t matchtag;
    bool inflight;
};

static void rpc_destroy (struct flux_rpc *rpc)
{
    if (rpc) {
        if (rpc->matchtag != FLUX_MATCHTAG_NONE && !rpc->inflight)
            flux_matchtag_free (rpc->h, rpc->matchtag);
        free (rpc);
    }
}

static struct flux_rpc *rpc_create (flux_t *h, int flags)
{
    struct flux_rpc *rpc;

    if (!(rpc = calloc (1, sizeof (*rpc)))) {
        errno = ENOMEM;
        goto error;
    }
    rpc->h = h;
    rpc->inflight = false;
    if ((flags & FLUX_RPC_NORESPONSE)) {
        rpc->matchtag = FLUX_MATCHTAG_NONE;
    }
    else {
        rpc->matchtag = flux_matchtag_alloc (h, 0);
        if (rpc->matchtag == FLUX_MATCHTAG_NONE)
            goto error;
    }
    return rpc;
error:
    free (rpc);
    return NULL;
}

int flux_rpc_get (flux_future_t *f, const char **json_str)
{
    const flux_msg_t *msg;
    int rc = -1;

    if (flux_future_get (f, &msg) < 0)
        goto done;
    if (flux_response_decode (msg, NULL, json_str) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_rpc_get_raw (flux_future_t *f, const void **data, int *len)
{
    const flux_msg_t *msg;
    int rc = -1;

    if (flux_future_get (f, &msg) < 0)
        goto done;
    if (flux_response_decode_raw (msg, NULL, data, len) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int flux_rpc_get_vunpack (flux_future_t *f, const char *fmt, va_list ap)
{
    const flux_msg_t *msg;
    int rc = -1;

    if (flux_future_get (f, &msg) < 0)
        goto done;
    if (flux_msg_vunpack (msg, fmt, ap) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_rpc_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_rpc_get_vunpack (f, fmt, ap);
    va_end (ap);

    return rc;
}

/* Message handler for response.
 * Parse the response message here so one could call flux_future_get()
 * instead of flux_rpc_get() to test result of RPC with no response payload.
 * Fulfill future.
*/
static void response_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    flux_future_t *f = arg;
    struct flux_rpc *rpc = flux_future_aux_get (f, "flux::rpc");
    flux_msg_t *cpy;
    int saved_errno;

    flux_msg_handler_stop (mh);
    rpc->inflight = false;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
#endif
#if HAVE_CALIPER
    cali_end_byname ("flux.message.rpc");
#endif
    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto error;
    flux_future_fulfill (f, cpy, (flux_free_f)flux_msg_destroy);
    return;
error:
    saved_errno = errno;
    flux_future_fulfill_error (f, saved_errno);
}

/* Callback to initialize future in main or alternate reactor contexts.
 * Install a message handler for the response.
 */
static void initialize_cb (flux_future_t *f, void *arg)
{
    struct flux_rpc *rpc = flux_future_aux_get (f, "flux::rpc");
    flux_msg_handler_t *mh;
    flux_t *h = flux_future_get_flux (f);
    struct flux_match m = FLUX_MATCH_RESPONSE;

    m.matchtag = rpc->matchtag;
    if (!(mh = flux_msg_handler_create (h, m, response_cb, f)))
        goto error;
    flux_msg_handler_allow_rolemask (mh, FLUX_ROLE_ALL);
    if (flux_future_aux_set (f, NULL, mh,
                             (flux_free_f)flux_msg_handler_destroy) < 0) {
        flux_msg_handler_destroy (mh);
        goto error;
    }
    flux_msg_handler_start (mh);
    return;
error:
    flux_future_fulfill_error (f, errno);
}

static flux_future_t *flux_rpc_msg (flux_t *h,
                                    uint32_t nodeid,
                                    int flags,
                                    flux_msg_t *msg)
{
    struct flux_rpc *rpc = NULL;
    flux_future_t *f;
    int msgflags = 0;

    if (!(f = flux_future_create (initialize_cb, NULL)))
        goto error;
    if (!(rpc = rpc_create (h, flags)))
        goto error;
    if (flux_future_aux_set (f, "flux::rpc", rpc,
                             (flux_free_f)rpc_destroy) < 0) {
        rpc_destroy (rpc);
        goto error;
    }
    if (flux_msg_set_matchtag (msg, rpc->matchtag) < 0)
        goto error;
    flux_future_set_flux (f, h);
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        msgflags |= FLUX_MSGFLAG_UPSTREAM;
        if (flux_get_rank (h, &nodeid) < 0)
            goto error;
    }
    if (flux_msg_set_nodeid (msg, nodeid, msgflags) < 0)
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
    cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    int rc = flux_send (h, msg, 0);
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc.nodeid");
    cali_end_byname ("flux.message.rpc");
#endif
    if (rc < 0)
        goto error;
    rpc->inflight = true;
    /* Fulfill future now if one-way
     */
    if ((flags & FLUX_RPC_NORESPONSE))
        flux_future_fulfill (f, NULL, NULL);
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

flux_future_t *flux_rpc (flux_t *h,
                         const char *topic,
                         const char *json_str,
                         uint32_t nodeid,
                         int flags)
{
    flux_msg_t *msg = NULL;
    flux_future_t *f = NULL;
    if (!(msg = flux_request_encode (topic, json_str)))
        goto done;
    if (!(f = flux_rpc_msg (h, nodeid, flags, msg)))
        goto done;
done:
    flux_msg_destroy (msg);
    return f;
}

flux_future_t *flux_rpc_raw (flux_t *h,
                             const char *topic,
                             const void *data,
                             int len,
                             uint32_t nodeid,
                             int flags)
{
    flux_msg_t *msg;
    flux_future_t *f = NULL;

    if (!(msg = flux_request_encode_raw (topic, data, len)))
        goto done;
    if (!(f = flux_rpc_msg (h, nodeid, flags, msg)))
        goto done;
done:
    flux_msg_destroy (msg);
    return f;
}

static flux_future_t *flux_rpc_vpack (flux_t *h,
                                      const char *topic,
                                      uint32_t nodeid,
                                      int flags,
                                      const char *fmt, va_list ap)
{
    flux_msg_t *msg;
    flux_future_t *f = NULL;

    if (!(msg = flux_request_encode (topic, NULL)))
        goto done;
    if (flux_msg_vpack (msg, fmt, ap) < 0)
        goto done;
    f = flux_rpc_msg (h, nodeid, flags, msg);
done:
    flux_msg_destroy (msg);
    return f;
}

flux_future_t *flux_rpc_pack (flux_t *h, const char *topic, uint32_t nodeid,
                              int flags, const char *fmt, ...)
{
    va_list ap;
    flux_future_t *f;

    va_start (ap, fmt);
    f = flux_rpc_vpack (h, topic, nodeid, flags, fmt, ap);
    va_end (ap);
    return f;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
