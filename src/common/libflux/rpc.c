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
    flux_match_t m;
    flux_t h;
    flux_then_f then_cb;
    void *then_arg;
    uint32_t *nodemap;          /* nodeid indexed by matchtag */
    zmsg_t *rx_msg;
    bool rx_msg_consumed;
    int rx_count;
};

void flux_rpc_destroy (flux_rpc_t rpc)
{
    if (rpc) {
        if (rpc->m.matchtag != FLUX_MATCHTAG_NONE)
            flux_matchtag_free (rpc->h, rpc->m.matchtag, rpc->m.bsize);
        zmsg_destroy (&rpc->rx_msg);
        if (rpc->nodemap)
            free (rpc->nodemap);
        free (rpc);
    }
}

static flux_rpc_t rpc_create (flux_t h, int count)
{
    flux_rpc_t rpc = xzmalloc (sizeof (*rpc));
    rpc->m.matchtag = flux_matchtag_alloc (h, count);
    if (rpc->m.matchtag == FLUX_MATCHTAG_NONE) {
        flux_rpc_destroy (rpc);
        return NULL;
    }
    rpc->m.bsize = count;
    rpc->m.typemask = FLUX_MSGTYPE_RESPONSE;
    rpc->h = h;
    rpc->nodemap = xzmalloc (sizeof (rpc->nodemap[0]) * count);
    return rpc;
}

flux_rpc_t flux_rpc (flux_t h, const char *topic, const char *json_str,
                     uint32_t nodeid, int flags)
{
    flux_rpc_t rpc = rpc_create (h, 1);
    zmsg_t *zmsg = NULL;

    if (!(zmsg = flux_request_encode (topic, json_str)))
        goto error;
    if (flux_msg_set_matchtag (zmsg, rpc->m.matchtag) < 0)
        goto error;
    if (flux_request_sendto (h, NULL, &zmsg, nodeid) < 0)
        goto error;
    rpc->nodemap[0] = nodeid;
    return rpc;
error:
    flux_rpc_destroy (rpc);
    zmsg_destroy (&zmsg);
    return NULL;
}

static uint32_t lookup_nodeid (flux_rpc_t rpc, uint32_t matchtag)
{
    int ix = matchtag - rpc->m.matchtag;
    if (ix < 0 || ix >= rpc->m.bsize)
        return FLUX_NODEID_ANY;
    return rpc->nodemap[ix];
}

static void garbage_collect (flux_rpc_t rpc)
{
    if (rpc->rx_msg && rpc->rx_msg_consumed)
        zmsg_destroy (&rpc->rx_msg);
}

static void rpc_addmsg (flux_rpc_t rpc, zmsg_t **zmsg)
{
    assert (rpc->rx_msg == NULL);
    rpc->rx_msg = *zmsg;
    *zmsg = NULL;
    rpc->rx_msg_consumed = false;
    rpc->rx_count++;
}

static int rpc_recvmsg (flux_rpc_t rpc, bool nonblock)
{
    assert (rpc->rx_msg == NULL);
    if (!(rpc->rx_msg = flux_recvmsg_match (rpc->h, rpc->m, NULL, nonblock)))
        return -1;
    rpc->rx_msg_consumed = false;
    rpc->rx_count++;
    return 0;
}

bool flux_rpc_check (flux_rpc_t rpc)
{
    garbage_collect (rpc);
    if (!rpc->rx_msg && rpc_recvmsg (rpc, true) < 0)
        errno = 0;
    if (rpc->rx_msg)
        return true;
    return false;
}

int flux_rpc_get (flux_rpc_t rpc, uint32_t *nodeid, const char **json_str)
{
    int rc = -1;

    garbage_collect (rpc);
    if (!rpc->rx_msg && rpc_recvmsg (rpc, false) < 0)
        goto done;
    rpc->rx_msg_consumed = true;
    if (flux_response_decode (rpc->rx_msg, NULL, json_str) < 0)
        goto done;
    if (nodeid) {
        uint32_t matchtag;
        if (flux_msg_get_matchtag (rpc->rx_msg, &matchtag) < 0)
            goto done;
        *nodeid = lookup_nodeid (rpc, matchtag);
    }
    rc = 0;
done:
    return rc;
}

/* N.B. if the user's 'then' callback doesn't call flux_rpc_get(),
 * the callback will be called again (ad infinitum)
 */
static int rpc_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    flux_rpc_t rpc = arg;

    assert (rpc->then_cb != NULL);
    do {
        garbage_collect (rpc);
        if (!rpc->rx_msg)
            rpc_addmsg (rpc, zmsg);
        rpc->then_cb (rpc, rpc->then_arg);
    } while (*zmsg != NULL);
    if (flux_rpc_completed (rpc))
        flux_msghandler_remove_match (rpc->h, rpc->m);
    return 0;
}

int flux_rpc_then (flux_rpc_t rpc, flux_then_f cb, void *arg)
{
    if (rpc->then_cb) {
        errno = EINVAL;
        return -1;
    }
    rpc->then_cb = cb;
    rpc->then_arg = arg;
    return flux_msghandler_add_match (rpc->h, rpc->m, rpc_cb, rpc);
}

bool flux_rpc_completed (flux_rpc_t rpc)
{
    if (rpc->rx_count == rpc->m.bsize && rpc->rx_msg_consumed)
        return true;
    return false;
}

flux_rpc_t flux_multrpc (flux_t h, const char *topic, const char *json_str,
                         const char *nodeset, int flags)
{
    nodeset_t ns = NULL;
    nodeset_itr_t itr = NULL;
    flux_rpc_t rpc = NULL;
    int i, count;
    zmsg_t *zmsg = NULL;

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
    if (!(rpc = rpc_create (h, count)))
        goto error;
    if (!(itr = nodeset_itr_new (ns)))
        goto error;
    for (i = 0; i < count; i++) {
        if (!(zmsg = flux_request_encode (topic, json_str)))
            goto error;
        rpc->nodemap[i] = nodeset_next (itr);
        assert (rpc->nodemap[i] != NODESET_EOF);
        if (flux_msg_set_matchtag (zmsg, rpc->m.matchtag + i) < 0)
            goto error;
        if (flux_request_sendto (h, NULL, &zmsg, rpc->nodemap[i]) < 0)
            goto error;
        assert (zmsg == NULL);
    }
    return rpc;
error:
    if (rpc)
        flux_rpc_destroy (rpc);
    if (itr)
        nodeset_itr_destroy (itr);
    if (ns)
        nodeset_destroy (ns);
    zmsg_destroy (&zmsg);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
