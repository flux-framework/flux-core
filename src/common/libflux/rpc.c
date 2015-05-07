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
#include "message.h"
#include "info.h"
#include "rpc.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

/* helper for flux_json_multrpc */
static int multrpc_cb (zmsg_t *zmsg, uint32_t nodeid,
                       flux_multrpc_f cb, void *arg)
{
    int errnum = 0;
    JSON out = NULL;

    if (flux_msg_get_errnum (zmsg, &errnum) < 0)
        errnum = errno;
    if (errnum == 0 && flux_msg_get_payload_json (zmsg, &out) < 0)
        errnum = errno;
    if (cb && cb (nodeid, errnum, out, arg) < 0)
        errnum = errno;
    Jput (out);
    if (errnum) {
        errno = errnum;
        return -1;
    }
    return 0;
}

int flux_json_multrpc (flux_t h, const char *nodeset, int fanout,
                       const char *topic, json_object *in,
                       flux_multrpc_f cb, void *arg)
{
    nodeset_t ns = nodeset_new_str (nodeset);
    nodeset_itr_t itr;
    int errnum = 0;
    uint32_t *nodeids = NULL;
    zlist_t *nomatch = NULL;
    int ntx, nrx, i;
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .topic_glob = NULL,
    };

    if (!(nomatch = zlist_new ()))
        oom ();
    if (!ns || nodeset_max (ns) >= flux_size (h)) {
        errnum = EINVAL;
        goto done;
    }

    /* Allocate block of matchtags.
     */
    match.bsize = nodeset_count (ns);
    match.matchtag = flux_matchtag_alloc (h, match.bsize);
    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errnum = EAGAIN;
        goto done;
    }

    /* Build map of matchtag -> nodeid
     */
    nodeids = xzmalloc (match.bsize * sizeof (nodeids[0]));
    if (!(itr = nodeset_itr_new (ns)))
        oom ();
    for (i = 0; i < match.bsize; i++)
        nodeids[i] = nodeset_next (itr);
    nodeset_itr_destroy (itr);

    /* Keep 'fanout' requests active concurrently
     */
    ntx = nrx = 0;
    while (ntx < match.bsize || nrx < match.bsize) {
        while (ntx < match.bsize && ntx - nrx < fanout) {
            uint32_t matchtag = match.matchtag + ntx;
            uint32_t nodeid = nodeids[ntx++];

            if (flux_json_request (h, nodeid, matchtag, topic, in) < 0) {
                if (errnum < errno)
                    errnum = errno;
                if (cb)
                    cb (nodeid, errno, NULL, arg);
                nrx++;
            }
        }
        while (nrx < match.bsize && (ntx - nrx == fanout || ntx == match.bsize)) {
            uint32_t matchtag;
            uint32_t nodeid;
            zmsg_t *zmsg;

            if (!(zmsg = flux_recvmsg_match (h, match, nomatch, false)))
                continue;
            if (flux_msg_get_matchtag (zmsg, &matchtag) < 0) {
                zmsg_destroy (&zmsg);
                continue;
            }
            nodeid = nodeids[matchtag - match.matchtag];
            if (multrpc_cb (zmsg, nodeid, cb, arg) < 0) {
                if (errnum < errno)
                    errnum = errno;
            }
            zmsg_destroy (&zmsg);
            nrx++;
        }
    }
    if (flux_putmsg_list (h, nomatch) < 0) {
        if (errnum < errno)
            errnum = errno;
    }
done:
    if (nodeids)
        free (nodeids);
    if (match.matchtag != FLUX_MATCHTAG_NONE)
        flux_matchtag_free (h, match.matchtag, match.bsize);
    if (nomatch)
        zlist_destroy (&nomatch);
    if (ns)
        nodeset_destroy (ns);
    if (errnum)
        errno = errnum;
    return errnum ? -1 : 0;
}

int flux_json_rpc (flux_t h, uint32_t nodeid, const char *topic,
                   JSON in, JSON *out)
{
    zmsg_t *zmsg = NULL;
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .matchtag = flux_matchtag_alloc (h, 1),
        .bsize = 1,
        .topic_glob = NULL,
    };
    int rc = -1;
    int errnum;
    JSON o;

    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errno = EAGAIN;
        goto done;
    }
    if (flux_json_request (h, nodeid, match.matchtag, topic, in) < 0)
        goto done;
    if (!(zmsg = flux_recvmsg_match (h, match, NULL, false)))
        goto done;
    if (flux_msg_get_errnum (zmsg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    if (flux_msg_get_payload_json (zmsg, &o) < 0)
        goto done;
    if ((!o && out)) {
        errno = EPROTO;
        goto done;
    }
    if ((o && !out)) {
        Jput (o);
        errno = EPROTO;
        goto done;
    }
    if (out)
        *out = o;
    rc = 0;
done:
    if (match.matchtag != FLUX_MATCHTAG_NONE)
        flux_matchtag_free (h, match.matchtag, match.bsize);
    zmsg_destroy (&zmsg);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
