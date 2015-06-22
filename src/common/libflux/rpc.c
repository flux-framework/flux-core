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
#include <czmq.h>

#include "request.h"
#include "response.h"
#include "message.h"
#include "info.h"
#include "rpc.h"
#include "reactor.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

typedef zlist_t zmsglist_t;

struct flux_rpc_struct {
    struct flux_match m;
    flux_t h;
    flux_then_f then_cb;
    void *then_arg;
    flux_msg_watcher_t *w;
    uint32_t *nodemap;          /* nodeid indexed by matchtag */
    zmsg_t *rx_msg;
    zmsg_t *rx_msg_consumed;
    int rx_count;
    bool oneway;
};

void flux_rpc_destroy (flux_rpc_t rpc)
{
    if (rpc) {
        if (rpc->w) {
            flux_msg_watcher_stop (rpc->h, rpc->w);
            flux_msg_watcher_destroy (rpc->w);
        }
        if (rpc->m.matchtag != FLUX_MATCHTAG_NONE) {
            /* FIXME: we cannot safely return matchtags to the pool here
             * if the rpc was not completed.  Lacking a proper cancellation
             * protocol, we simply leak them.  See issue #212.
             */
            if (flux_rpc_completed (rpc))
                flux_matchtag_free (rpc->h, rpc->m.matchtag, rpc->m.bsize);
        }
        zmsg_destroy (&rpc->rx_msg);
        zmsg_destroy (&rpc->rx_msg_consumed);
        if (rpc->nodemap)
            free (rpc->nodemap);
        free (rpc);
    }
}

static flux_rpc_t rpc_create (flux_t h, int flags, int count)
{
    flux_rpc_t rpc = xzmalloc (sizeof (*rpc));
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
    return rpc;
}

static uint32_t lookup_nodeid (flux_rpc_t rpc, uint32_t matchtag)
{
    int ix = matchtag - rpc->m.matchtag;
    if (ix < 0 || ix >= rpc->m.bsize)
        return FLUX_NODEID_ANY;
    return rpc->nodemap[ix];
}

static int rpc_request_send (flux_rpc_t rpc, int n, const char *topic,
                             const char *json_str, uint32_t nodeid)
{
    flux_msg_t *msg;
    int flags = 0;
    int rc = -1;

    if (!(msg = flux_request_encode (topic, json_str)))
        goto done;
    if (flux_msg_set_matchtag (msg, rpc->m.matchtag + n) < 0)
        goto done;
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        flags |= FLUX_MSGFLAG_UPSTREAM;
        nodeid = flux_rank (rpc->h);
    }
    if (flux_msg_set_nodeid (msg, nodeid, flags) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    if (msg)
        flux_msg_destroy (msg);
    return rc;
}


bool flux_rpc_check (flux_rpc_t rpc)
{
    if (rpc->oneway)
        return false;
    if (rpc->rx_msg || (rpc->rx_msg = flux_recv (rpc->h, rpc->m,
                                                 FLUX_O_NONBLOCK)))
        return true;
    errno = 0;
    return false;
}

int flux_rpc_get (flux_rpc_t rpc, uint32_t *nodeid, const char **json_str)
{
    int rc = -1;

    if (rpc->oneway) {
        errno = EINVAL;
        goto done;
    }
    if (!rpc->rx_msg && !(rpc->rx_msg = flux_recv (rpc->h, rpc->m, 0)))
        goto done;
    zmsg_destroy (&rpc->rx_msg_consumed); /* invalidate last-got payload */
    rpc->rx_msg_consumed = rpc->rx_msg;
    rpc->rx_msg = NULL;
    rpc->rx_count++;
    if (nodeid) {
        uint32_t matchtag;
        if (flux_msg_get_matchtag (rpc->rx_msg_consumed, &matchtag) < 0)
            goto done;
        *nodeid = lookup_nodeid (rpc, matchtag);
    }
    if (flux_response_decode (rpc->rx_msg_consumed, NULL, json_str) < 0)
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
static void rpc_cb (flux_t h, flux_msg_watcher_t *w,
                    const flux_msg_t *msg, void *arg)
{
    flux_rpc_t rpc = arg;
    assert (rpc->then_cb != NULL);

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
    ;
}

int flux_rpc_then (flux_rpc_t rpc, flux_then_f cb, void *arg)
{
    int rc = -1;

    if (rpc->oneway) {
        errno = EINVAL;
        goto done;
    }
    if (cb && !rpc->then_cb) {
        if (!rpc->w) {
            if (!(rpc->w = flux_msg_watcher_create (rpc->m, rpc_cb, rpc)))
                goto done;
        }
        flux_msg_watcher_start (rpc->h, rpc->w);
        if (rpc->rx_msg) {
            if (flux_requeue (rpc->h, rpc->rx_msg, FLUX_RQ_HEAD) < 0)
                goto done;
            rpc->rx_msg = NULL;
        }
    } else if (!cb && rpc->then_cb) {
        flux_msg_watcher_stop (rpc->h, rpc->w);
    }
    rpc->then_cb = cb;
    rpc->then_arg = arg;
    rc = 0;
done:
    return rc;
}

bool flux_rpc_completed (flux_rpc_t rpc)
{
    if (rpc->oneway || rpc->rx_count == rpc->m.bsize)
        return true;
    return false;
}

flux_rpc_t flux_rpc (flux_t h, const char *topic, const char *json_str,
                     uint32_t nodeid, int flags)
{
    flux_rpc_t rpc = rpc_create (h, flags, 1);

    if (rpc_request_send (rpc, 0, topic, json_str, nodeid) < 0)
        goto error;
    if (!rpc->oneway)
        rpc->nodemap[0] = nodeid;
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

flux_rpc_t flux_rpc_multi (flux_t h, const char *topic, const char *json_str,
                           const char *nodeset, int flags)
{
    nodeset_t ns = NULL;
    nodeset_itr_t itr = NULL;
    flux_rpc_t rpc = NULL;
    int i, count;

    if (!topic || !nodeset) {
        errno = EINVAL;
        goto error;
    }
    if (!strcmp (nodeset, "all")) {
        count = flux_size (h);
        ns = nodeset_new_range (0, count - 1);
    } else {
        if ((ns = nodeset_new_str (nodeset)))
            count = nodeset_count (ns);
    }
    if (!ns) {
        errno = EINVAL;
        goto error;
    }
    if (!(rpc = rpc_create (h, flags, count)))
        goto error;
    if (!(itr = nodeset_itr_new (ns)))
        goto error;
    for (i = 0; i < count; i++) {
        uint32_t nodeid = nodeset_next (itr);
        assert (nodeid != NODESET_EOF);
        if (rpc_request_send (rpc, i, topic, json_str, nodeid) < 0)
            goto error;
        if (!rpc->oneway)
            rpc->nodemap[i] = nodeid;
    }
    return rpc;
error:
    if (rpc)
        flux_rpc_destroy (rpc);
    if (itr)
        nodeset_itr_destroy (itr);
    if (ns)
        nodeset_destroy (ns);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
