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

#include "request.h"
#include "response.h"
#include "message.h"
#include "info.h"
#include "rpc.h"
#include "reactor.h"
#include "dispatch.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

struct flux_rpc_struct {
    struct flux_match m;
    flux_t h;
    flux_then_f then_cb;
    void *then_arg;
    flux_msg_handler_t *w;
    uint32_t *nodemap;          /* nodeid indexed by matchtag */
    flux_msg_t *rx_msg;
    flux_msg_t *rx_msg_consumed;
    int rx_count;
    bool oneway;
    void *aux;
    flux_free_f aux_destroy;
    const char *type;
    int usecount;
};

static void flux_rpc_usecount_incr (flux_rpc_t *rpc)
{
    rpc->usecount++;
}

static void flux_rpc_usecount_decr (flux_rpc_t *rpc)
{
    if (rpc && --rpc->usecount == 0) {
        if (rpc->w) {
            flux_msg_handler_stop (rpc->w);
            flux_msg_handler_destroy (rpc->w);
        }
        if (rpc->m.matchtag != FLUX_MATCHTAG_NONE) {
            /* FIXME: we cannot safely return matchtags to the pool here
             * if the rpc was not completed.  Lacking a proper cancellation
             * protocol, we simply leak them.  See issue #212.
             */
            if (flux_rpc_completed (rpc))
                flux_matchtag_free (rpc->h, rpc->m.matchtag, rpc->m.bsize);
        }
        flux_msg_destroy (rpc->rx_msg);
        flux_msg_destroy (rpc->rx_msg_consumed);
        if (rpc->nodemap)
            free (rpc->nodemap);
        if (rpc->aux && rpc->aux_destroy)
            rpc->aux_destroy (rpc->aux);
        free (rpc);
    }
}

void flux_rpc_destroy (flux_rpc_t *rpc)
{
    flux_rpc_usecount_decr (rpc);
}

static flux_rpc_t *rpc_create (flux_t h, int flags, int count)
{
    flux_rpc_t *rpc = xzmalloc (sizeof (*rpc));
    if ((flags & FLUX_RPC_NORESPONSE)) {
        rpc->oneway = true;
        rpc->m.matchtag = FLUX_MATCHTAG_NONE;
    } else {
        rpc->nodemap = xzmalloc (sizeof (rpc->nodemap[0]) * count);
        rpc->m.matchtag = flux_matchtag_alloc (h, count);
        if (rpc->m.matchtag == FLUX_MATCHTAG_NONE) {
            flux_rpc_destroy (rpc);
            return NULL;
        }
    }
    rpc->m.bsize = count;
    rpc->m.typemask = FLUX_MSGTYPE_RESPONSE;
    rpc->h = h;
    rpc->usecount = 1;
    return rpc;
}

static uint32_t lookup_nodeid (flux_rpc_t *rpc, uint32_t matchtag)
{
    int ix = matchtag - rpc->m.matchtag;
    if (ix < 0 || ix >= rpc->m.bsize)
        return FLUX_NODEID_ANY;
    return rpc->nodemap[ix];
}

static int rpc_request_prepare (flux_rpc_t *rpc, int n, flux_msg_t *msg,
                                uint32_t nodeid)
{
    int flags = 0;
    int rc = -1;

    if (flux_msg_set_matchtag (msg, rpc->m.matchtag + n) < 0)
        goto done;
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        flags |= FLUX_MSGFLAG_UPSTREAM;
        if (flux_get_rank (rpc->h, &nodeid) < 0)
            goto done;
    }
    if (flux_msg_set_nodeid (msg, nodeid, flags) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int rpc_request_send (flux_rpc_t *rpc, int n, const char *topic,
                             const char *json_str, uint32_t nodeid)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_request_encode (topic, json_str)))
        goto done;
    if (rpc_request_prepare (rpc, n, msg, nodeid) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    if (msg)
        flux_msg_destroy (msg);
    return rc;
}

static int rpc_request_send_raw (flux_rpc_t *rpc, int n, const char *topic,
                                 const void *data, int len, uint32_t nodeid)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_request_encode_raw (topic, data, len)))
        goto done;
    if (rpc_request_prepare (rpc, n, msg, nodeid) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    if (msg)
        flux_msg_destroy (msg);
    return rc;
}

bool flux_rpc_check (flux_rpc_t *rpc)
{
    if (rpc->oneway)
        return false;
    if (rpc->rx_msg || (rpc->rx_msg = flux_recv (rpc->h, rpc->m,
                                                 FLUX_O_NONBLOCK)))
        return true;
    errno = 0;
    return false;
}

static int rpc_get (flux_rpc_t *rpc, uint32_t *nodeid)
{
    int rc = -1;

    if (rpc->oneway) {
        errno = EINVAL;
        goto done;
    }
    if (!rpc->rx_msg && !(rpc->rx_msg = flux_recv (rpc->h, rpc->m, 0)))
        goto done;
    flux_msg_destroy (rpc->rx_msg_consumed); /* invalidate last-got payload */
    rpc->rx_msg_consumed = rpc->rx_msg;
    rpc->rx_msg = NULL;
    rpc->rx_count++;
    if (nodeid) {
        uint32_t matchtag;
        if (flux_msg_get_matchtag (rpc->rx_msg_consumed, &matchtag) < 0)
            goto done;
        *nodeid = lookup_nodeid (rpc, matchtag);
    }
    rc = 0;
done:
    return rc;
}

int flux_rpc_get (flux_rpc_t *rpc, uint32_t *nodeid, const char **json_str)
{
    int rc = -1;

    if (rpc_get (rpc, nodeid) < 0)
        goto done;
    if (flux_response_decode (rpc->rx_msg_consumed, NULL, json_str) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_rpc_get_raw (flux_rpc_t *rpc, uint32_t *nodeid, void *data, int *len)
{
    int rc = -1;

    if (rpc_get (rpc, nodeid) < 0)
        goto done;
    if (flux_response_decode_raw (rpc->rx_msg_consumed, NULL, data, len) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

/* N.B. if a new message arrives with an unconsumed one in the rpc handle,
 * push the new one back to to the receive queue so it will trigger another
 * reactor callback and handle the cached one now.
 * The reactor will repeatedly call the continuation (level-triggered)
 * until all received responses are consumed.
 */
static void rpc_cb (flux_t h, flux_msg_handler_t *w,
                    const flux_msg_t *msg, void *arg)
{
    flux_rpc_t *rpc = arg;
    assert (rpc->then_cb != NULL);

    flux_rpc_usecount_incr (rpc);
    if (rpc->rx_msg) {
        if (flux_requeue (rpc->h, msg, FLUX_RQ_HEAD) < 0)
            goto done;
    } else {
        if (!(rpc->rx_msg = flux_msg_copy (msg, true)))
            goto done;
    }
    rpc->then_cb (rpc, rpc->then_arg);
    if (rpc->rx_msg) {
        if (flux_requeue (rpc->h, rpc->rx_msg, FLUX_RQ_HEAD) < 0)
            goto done;
        rpc->rx_msg = NULL;
    }
done: /* no good way to report flux_requeue() errors */
    if (flux_rpc_completed (rpc))
        flux_msg_handler_stop (rpc->w);
    flux_rpc_usecount_decr (rpc);
}

int flux_rpc_then (flux_rpc_t *rpc, flux_then_f cb, void *arg)
{
    int rc = -1;

    if (rpc->oneway) {
        errno = EINVAL;
        goto done;
    }
    if (cb && !rpc->then_cb) {
        if (!rpc->w) {
            if (!(rpc->w = flux_msg_handler_create (rpc->h, rpc->m,
                                                    rpc_cb, rpc)))
                goto done;
        }
        flux_msg_handler_start (rpc->w);
        if (rpc->rx_msg) {
            if (flux_requeue (rpc->h, rpc->rx_msg, FLUX_RQ_HEAD) < 0)
                goto done;
            rpc->rx_msg = NULL;
        }
    } else if (!cb && rpc->then_cb) {
        flux_msg_handler_stop (rpc->w);
    }
    rpc->then_cb = cb;
    rpc->then_arg = arg;
    rc = 0;
done:
    return rc;
}

bool flux_rpc_completed (flux_rpc_t *rpc)
{
    if (rpc->oneway || rpc->rx_count == rpc->m.bsize)
        return true;
    return false;
}

flux_rpc_t *flux_rpc (flux_t h, const char *topic, const char *json_str,
                      uint32_t nodeid, int flags)
{
    flux_rpc_t *rpc;

    if (!(rpc = rpc_create (h, flags, 1)))
        goto error;
    if (rpc_request_send (rpc, 0, topic, json_str, nodeid) < 0)
        goto error;
    if (!rpc->oneway)
        rpc->nodemap[0] = nodeid;
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

flux_rpc_t *flux_rpc_raw (flux_t h, const char *topic,
                          const void *data, int len,
                          uint32_t nodeid, int flags)
{
    flux_rpc_t *rpc;

    if (!(rpc = rpc_create (h, flags, 1)))
        goto error;
    if (rpc_request_send_raw (rpc, 0, topic, data, len, nodeid) < 0)
        goto error;
    if (!rpc->oneway)
        rpc->nodemap[0] = nodeid;
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

flux_rpc_t *flux_rpc_multi (flux_t h, const char *topic, const char *json_str,
                            const char *nodeset, int flags)
{
    nodeset_t *ns = NULL;
    nodeset_iterator_t *itr = NULL;
    flux_rpc_t *rpc = NULL;
    int i;
    uint32_t count;

    if (!topic || !nodeset) {
        errno = EINVAL;
        goto error;
    }
    if (!strcmp (nodeset, "all")) {
        if (flux_get_size (h, &count) < 0)
            goto error;
        ns = nodeset_create_range (0, count - 1);
    } else {
        if ((ns = nodeset_create_string (nodeset)))
            count = nodeset_count (ns);
    }
    if (!ns) {
        errno = EINVAL;
        goto error;
    }
    if (!(rpc = rpc_create (h, flags, count)))
        goto error;
    if (!(itr = nodeset_iterator_create (ns)))
        goto error;
    for (i = 0; i < count; i++) {
        uint32_t nodeid = nodeset_next (itr);
        assert (nodeid != NODESET_EOF);
        if (rpc_request_send (rpc, i, topic, json_str, nodeid) < 0)
            goto error;
        if (!rpc->oneway)
            rpc->nodemap[i] = nodeid;
    }
    nodeset_iterator_destroy (itr);
    return rpc;
error:
    if (rpc)
        flux_rpc_destroy (rpc);
    if (itr)
        nodeset_iterator_destroy (itr);
    if (ns)
        nodeset_destroy (ns);
    return NULL;
}

const char *flux_rpc_type_get (flux_rpc_t *rpc)
{
    return rpc->type;
}

void flux_rpc_type_set (flux_rpc_t *rpc, const char *type)
{
    rpc->type = type;
}

void *flux_rpc_aux_get (flux_rpc_t *rpc)
{
    return rpc->aux;
}

void flux_rpc_aux_set (flux_rpc_t *rpc, void *aux, flux_free_f destroy)
{
    if (rpc->aux && rpc->aux_destroy)
        rpc->aux_destroy (rpc->aux);
    rpc->aux = aux;
    rpc->aux_destroy = destroy;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
