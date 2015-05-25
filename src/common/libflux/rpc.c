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

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

struct response_struct {
    zmsg_t *zmsg;
    int errnum;
};

struct flux_mresponse_struct {
    uint32_t count;
    uint32_t matchbase;
    struct response_struct *r;
    zhash_t *bynodeid;
};

int flux_rpcto (flux_t h, const char *topic,
                const char *json_str, zmsg_t **response, uint32_t nodeid)
{
    zmsg_t *tx_zmsg = NULL;
    zmsg_t *rx_zmsg = NULL;
    uint32_t matchtag = FLUX_MATCHTAG_NONE;
    int errnum;
    int rc = -1;

    if (!(tx_zmsg = flux_request_encode (topic, json_str)))
        goto done;
    if (flux_request_sendto (h, &matchtag, &tx_zmsg, nodeid) < 0)
        goto done;
    if (!(rx_zmsg = flux_response_recv (h, matchtag, false)))
        goto done;
    if (flux_msg_get_errnum (rx_zmsg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    if (response) {
        *response = rx_zmsg;
        rx_zmsg = NULL;
    }
    rc = 0;
done:
    if (matchtag != FLUX_MATCHTAG_NONE)
        flux_matchtag_free (h, matchtag, 1);
    zmsg_destroy (&tx_zmsg);
    zmsg_destroy (&rx_zmsg);
    return rc;
}

int flux_rpc (flux_t h, const char *topic,
              const char *json_str, zmsg_t **response)
{
    return flux_rpcto (h, topic, json_str, response, FLUX_NODEID_ANY);
}

void flux_mresponse_destroy (flux_mresponse_t r)
{
    if (r) {
        if (r->r) {
            int i;
            for (i = 0; i < r->count; i++)
                zmsg_destroy (&r->r[i].zmsg);
            free (r->r);
        }
        zhash_destroy (&r->bynodeid);
        free (r);
    }
}

static void mresponse_create_hash (flux_mresponse_t r, nodeset_t ns)
{
    nodeset_itr_t itr;
    uint32_t nodeid;
    char tmp[16];
    int i = 0;

    if (!r->bynodeid) {
        if (!(r->bynodeid = zhash_new()))
            oom ();
        if (!(itr = nodeset_itr_new (ns)))
            oom ();
        for (i = 0; i < r->count; i++) {
            nodeid = nodeset_next (itr);
            assert (nodeid != NODESET_EOF);
            snprintf (tmp, sizeof (tmp), "%u", nodeid);
            zhash_update (r->bynodeid, tmp, &r->r[i]);
        }
        nodeset_itr_destroy (itr);
    }
}

static flux_mresponse_t mresponse_create (nodeset_t ns, uint32_t matchbase)
{
    flux_mresponse_t r = xzmalloc (sizeof (*r));

    r->count = nodeset_count (ns);
    r->matchbase = matchbase;
    if (!(r->r = xzmalloc (sizeof (r->r[0]) * r->count)))
        oom ();
    mresponse_create_hash (r, ns);
    return r;
}

static void mresponse_set_zmsg (flux_mresponse_t r, uint32_t matchtag,
                                zmsg_t **zmsg)
{
    int i = matchtag - r->matchbase;
    assert (i >= 0 && i < r->count);
    r->r[i].zmsg = *zmsg;
    *zmsg = NULL;
}

static void mresponse_set_errnum (flux_mresponse_t r, uint32_t matchtag,
                                  int errnum)
{
    int i = matchtag - r->matchbase;
    assert (i >= 0 && i < r->count);
    r->r[i].errnum = errnum;
}

static int mresponse_decode_all (flux_mresponse_t r) {
    int i, errnum = 0;

    for (i = 0; i < r->count; i++) {
        int e = r->r[i].errnum;
        if (e == 0 && r->r[i].zmsg)
            if (flux_msg_get_errnum (r->r[i].zmsg, &e) < 0)
                e = errno;
        if (errnum == 0 || errnum < e)
            errnum = e;
    }
    if (errnum) {
        errno = errnum;
        return -1;
    }
    return 0;
}

int flux_mresponse_decode (flux_mresponse_t r, uint32_t nodeid,
                           const char **topic, const char **json_str)
{
    char tmp[16];
    struct response_struct *res;
    int rc = -1;

    snprintf (tmp, sizeof (tmp), "%u", nodeid);
    if (!(res = zhash_lookup (r->bynodeid, tmp))) {
        errno = ENOENT;
        goto done;
    }
    if (res->errnum != 0) {
        errno = res->errnum;
        goto done;
    }
    if (res->zmsg && flux_response_decode (res->zmsg, topic, json_str) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_multrpcto (flux_t h, int fanout,
                   const char *topic, const char *json_str,
                   flux_mresponse_t *mresponse, const char *nodeset)
{
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .topic_glob = NULL,
    };
    int saved_errno;
    flux_mresponse_t r = NULL;
    nodeset_t ns = NULL;
    nodeset_itr_t itr = NULL;
    zlist_t *nomatch = NULL;
    int ntx, nrx;
    int rc = -1;

    if (!topic) {
        errno = EINVAL;
        goto done;
    }
    if (fanout == 0)
        fanout = INT_MAX;
    if (nodeset)
        ns = nodeset_new_str (nodeset);
    else
        ns = nodeset_new_range (0, flux_size (h) - 1);
    if (!ns)
        goto done;
    /* Allocate a block of matchtags
     */
    match.bsize = nodeset_count (ns);
    match.matchtag = flux_matchtag_alloc (h, match.bsize);
    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errno = EAGAIN;
        goto done;
    }
    /* Allocate result object
     */
    if (!(r = mresponse_create (ns, match.matchtag)))
        goto done;

    /* Send requests and receive responses, keeping a maximum of
     * 'fanout' requests outstanding.
     */
    if (!(nomatch = zlist_new ()))
        oom ();
    ntx = nrx = 0;
    if (!(itr = nodeset_itr_new (ns)))
        oom ();
    while (ntx < match.bsize || nrx < match.bsize) {
        while (ntx < match.bsize && ntx - nrx < fanout) {
            uint32_t matchtag = match.matchtag + ntx;
            uint32_t nodeid = nodeset_next (itr);
            assert (nodeid != NODESET_EOF);
            zmsg_t *zmsg;

            if (!(zmsg = flux_request_encode (topic, json_str))
                     || flux_msg_set_matchtag (zmsg, matchtag) < 0
                     || flux_request_sendto (h, NULL, &zmsg, nodeid) < 0) {
                mresponse_set_errnum (r, matchtag, errno);
                nrx++;
            }
            zmsg_destroy (&zmsg);
            ntx++;
        }
        while (nrx < match.bsize
                    && (ntx - nrx == fanout || ntx == match.bsize)) {
            uint32_t matchtag;
            zmsg_t *zmsg;

            if (!(zmsg = flux_recvmsg_match (h, match, nomatch, false)))
                continue;
            if (flux_msg_get_matchtag (zmsg, &matchtag) < 0) {
                zmsg_destroy (&zmsg);
                continue;
            }
            mresponse_set_zmsg (r, matchtag, &zmsg);
            zmsg_destroy (&zmsg);
            nrx++;
        }
    }
    if (flux_putmsg_list (h, nomatch) < 0)
        goto done;
    if (mresponse) {
        *mresponse = r;
        r = NULL;
    } else {
        if (mresponse_decode_all (r) < 0)
            goto done;
    }
    rc = 0;
done:
    saved_errno = errno;
    if (r)
        flux_mresponse_destroy (r);
    if (itr)
        nodeset_itr_destroy (itr);
    if (ns)
        nodeset_destroy (ns);
    if (match.matchtag != FLUX_MATCHTAG_NONE)
        flux_matchtag_free (h, match.matchtag, match.bsize);
    if (nomatch)
        zlist_destroy (&nomatch);
    errno = saved_errno;
    return rc;
}

int flux_multrpc (flux_t h, int fanout,
                  const char *topic, const char *json_str,
                  flux_mresponse_t *mresponse)
{
    return flux_multrpcto (h, fanout, topic, json_str, mresponse, NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
