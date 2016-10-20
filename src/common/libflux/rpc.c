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
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif
#include <jansson.h>

#include "request.h"
#include "response.h"
#include "message.h"
#include "info.h"
#include "rpc.h"
#include "reactor.h"
#include "dispatch.h"

#include "src/common/libutil/nodeset.h"

#define RPC_MAGIC 0x114422af

struct flux_rpc_struct {
    int magic;
    struct flux_match m;
    flux_t *h;
    flux_then_f then_cb;
    void *then_arg;
    flux_msg_handler_t *w;
    uint32_t nodeid;
    flux_msg_t *rx_msg;
    int rx_errnum;
    int rx_count;
    int rx_expected;
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
    if (!rpc)
        return;
    assert (rpc->magic == RPC_MAGIC);
    if (--rpc->usecount == 0) {
        if (rpc->w) {
            flux_msg_handler_stop (rpc->w);
            flux_msg_handler_destroy (rpc->w);
        }
        if (rpc->m.matchtag != FLUX_MATCHTAG_NONE) {
            /* FIXME: we cannot safely return matchtags to the pool here
             * if the rpc was not completed.  Lacking a proper cancellation
             * protocol, we simply leak them.  See issue #212.
             */
            if (rpc->rx_count >= rpc->rx_expected)
                flux_matchtag_free (rpc->h, rpc->m.matchtag);
        }
        flux_msg_destroy (rpc->rx_msg);
        if (rpc->aux && rpc->aux_destroy)
            rpc->aux_destroy (rpc->aux);
        rpc->magic =~ RPC_MAGIC;
        free (rpc);
    }
}

void flux_rpc_destroy (flux_rpc_t *rpc)
{
    int saved_errno = errno;
    flux_rpc_usecount_decr (rpc);
    errno = saved_errno;
}

static flux_rpc_t *rpc_create (flux_t *h, int rx_expected)
{
    flux_rpc_t *rpc;
    int flags = 0;

    if (!(rpc = calloc (1, sizeof (*rpc)))) {
        errno = ENOMEM;
        return NULL;
    }
    rpc->magic = RPC_MAGIC;
    if (rx_expected == 0) {
        rpc->m.matchtag = FLUX_MATCHTAG_NONE;
    } else {
        if (rx_expected != 1)
            flags |= FLUX_MATCHTAG_GROUP;
        rpc->m.matchtag = flux_matchtag_alloc (h, flags);
        if (rpc->m.matchtag == FLUX_MATCHTAG_NONE) {
            flux_rpc_destroy (rpc);
            return NULL;
        }
    }
    rpc->rx_expected = rx_expected;
    rpc->m.typemask = FLUX_MSGTYPE_RESPONSE;
    rpc->h = h;
    rpc->usecount = 1;
    rpc->nodeid = FLUX_NODEID_ANY;
    return rpc;
}

static int rpc_request_prepare (flux_rpc_t *rpc, flux_msg_t *msg,
                                uint32_t nodeid)
{
    int flags = 0;
    int rc = -1;
    uint32_t matchtag = rpc->m.matchtag & ~FLUX_MATCHTAG_GROUP_MASK;
    uint32_t matchgrp = rpc->m.matchtag & FLUX_MATCHTAG_GROUP_MASK;

    /* So that flux_rpc_get_nodeid() can get nodeid from a response:
     * For group rpc, use the lower matchtag bits to stash the nodeid
     * in the request, and it will come back in the response.
     * For singleton rpc, stash destination nodeid in rpc->nodeid.
     * TODO-scalability: If nodeids exceed 1<<20, we may need to use a seq#
     * in lower matchtag bits and stash a mapping array inthe rpc object.
     */
    if (matchgrp > 0) {
        if ((nodeid & FLUX_MATCHTAG_GROUP_MASK) != 0) {
            errno = ERANGE;
            goto done;
        }
        matchtag = matchgrp | nodeid;
    } else
        rpc->nodeid = nodeid;
    if (flux_msg_set_matchtag (msg, matchtag) < 0)
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

static int rpc_request_send (flux_rpc_t *rpc, const char *topic,
                             uint32_t nodeid, const char *json_str)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_request_encode (topic, json_str)))
        goto done;
    if (rpc_request_prepare (rpc, msg, nodeid) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int rpc_request_vsendf (flux_rpc_t *rpc, const char *topic,
                               uint32_t nodeid, const char *fmt, va_list ap)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_request_encode (topic, NULL)))
        goto done;
    if (flux_msg_vset_jsonf (msg, fmt, ap) < 0)
        goto done;
    if (rpc_request_prepare (rpc, msg, nodeid) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int rpc_request_send_raw (flux_rpc_t *rpc, const char *topic,
                                 uint32_t nodeid, const void *data, int len)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_request_encode_raw (topic, data, len)))
        goto done;
    if (rpc_request_prepare (rpc, msg, nodeid) < 0)
        goto done;
    if (flux_send (rpc->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

bool flux_rpc_check (flux_rpc_t *rpc)
{
    assert (rpc->magic == RPC_MAGIC);
    if (rpc->rx_msg || rpc->rx_errnum)
        return true;
    if (!(rpc->rx_msg = flux_recv (rpc->h, rpc->m, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            errno = 0;
            return false;
        } else
            rpc->rx_errnum = errno;
    }
    return (rpc->rx_msg || rpc->rx_errnum);
}

static int rpc_get (flux_rpc_t *rpc)
{
    int rc = -1;

    if (rpc->rx_errnum) {
        errno = rpc->rx_errnum;
        goto done;
    }
    if (!rpc->rx_msg && !rpc->rx_errnum) {
#if HAVE_CALIPER
        cali_begin_string_byname("flux.message.rpc", "single");
#endif
        if (!(rpc->rx_msg = flux_recv (rpc->h, rpc->m, 0))) {
            rpc->rx_errnum = errno;
            goto done;
        }
#if HAVE_CALIPER
        cali_end_byname("flux.message.rpc");
#endif
        rpc->rx_count++;
    }
    rc = 0;
done:
    return rc;
}

int flux_rpc_get (flux_rpc_t *rpc, const char **json_str)
{
    int rc = -1;

    assert (rpc->magic == RPC_MAGIC);
    if (rpc_get (rpc) < 0)
        goto done;
    if (flux_response_decode (rpc->rx_msg, NULL, json_str) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_rpc_get_raw (flux_rpc_t *rpc, void *data, int *len)
{
    int rc = -1;

    assert (rpc->magic == RPC_MAGIC);
    if (rpc_get (rpc) < 0)
        goto done;
    if (flux_response_decode_raw (rpc->rx_msg, NULL, data, len) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int flux_rpc_vgetf (flux_rpc_t *rpc, const char *fmt, va_list ap)
{
    int rc = -1;
    const char *json_str;

    assert (rpc->magic == RPC_MAGIC);
    if (rpc_get (rpc) < 0)
        goto done;
    if (flux_response_decode (rpc->rx_msg, NULL, &json_str) < 0)
        goto done;
    if (flux_msg_vget_jsonf (rpc->rx_msg, fmt, ap) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_rpc_getf (flux_rpc_t *rpc, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_rpc_vgetf (rpc, fmt, ap);
    va_end (ap);

    return rc;
}

int flux_rpc_get_nodeid (flux_rpc_t *rpc, uint32_t *nodeid)
{
    int rc = -1;
    uint32_t tag;

    assert (rpc->magic == RPC_MAGIC);
    if (rpc_get (rpc) < 0)
        goto done;
    if (flux_msg_get_matchtag (rpc->rx_msg, &tag) < 0)
        goto done;
    if ((tag & FLUX_MATCHTAG_GROUP_MASK) > 0)
        *nodeid = tag & ~FLUX_MATCHTAG_GROUP_MASK;
    else
        *nodeid = rpc->nodeid;
    rc = 0;
done:
    return rc;
}

/* Internal callback for matching response.
 * For the multi-response case, overwrite previous message if
 * flux_rpc_next () was not called.
 */
static void rpc_cb (flux_t *h, flux_msg_handler_t *w,
                    const flux_msg_t *msg, void *arg)
{
    flux_rpc_t *rpc = arg;
    assert (rpc->then_cb != NULL);

    flux_rpc_usecount_incr (rpc);
    if (rpc->rx_msg || rpc->rx_errnum)
        (void)flux_rpc_next (rpc);
    if (!(rpc->rx_msg = flux_msg_copy (msg, true)))
        goto done;
    rpc->rx_count++;
    rpc->then_cb (rpc, rpc->then_arg);
done:
    if (rpc->rx_count >= rpc->rx_expected || flux_fatality (rpc->h))
        flux_msg_handler_stop (rpc->w);
    flux_rpc_usecount_decr (rpc);
}

int flux_rpc_then (flux_rpc_t *rpc, flux_then_f cb, void *arg)
{
    int rc = -1;

    assert (rpc->magic == RPC_MAGIC);
    if (rpc->rx_count >= rpc->rx_expected) {
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
        if (rpc->rx_msg || rpc->rx_errnum) {
            if (rpc->rx_msg)
                if (flux_requeue (rpc->h, rpc->rx_msg, FLUX_RQ_HEAD) < 0)
                    goto done;
            (void)flux_rpc_next (rpc);
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

int flux_rpc_next (flux_rpc_t *rpc)
{
    assert (rpc->magic == RPC_MAGIC);
    if (flux_fatality (rpc->h))
        return -1;
    if (rpc->rx_count >= rpc->rx_expected)
        return -1;
    flux_msg_destroy (rpc->rx_msg);
    rpc->rx_msg = NULL;
    rpc->rx_errnum = 0;
    return 0;
}

flux_rpc_t *flux_rpc (flux_t *h,
                      const char *topic,
                      const char *json_str,
                      uint32_t nodeid,
                      int flags)
{
    flux_rpc_t *rpc;
    int rx_expected = 1;

    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(rpc = rpc_create (h, rx_expected)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
    cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    if (rpc_request_send (rpc, topic, nodeid, json_str) < 0)
        goto error;
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc.nodeid");
    cali_end_byname ("flux.message.rpc");
#endif
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

flux_rpc_t *flux_rpc_raw (flux_t *h,
                          const char *topic,
                          const void *data,
                          int len,
                          uint32_t nodeid,
                          int flags)
{
    flux_rpc_t *rpc;
    int rx_expected = 1;

    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(rpc = rpc_create (h, rx_expected)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    if (rpc_request_send_raw (rpc, topic, nodeid, data, len) < 0)
        goto error;
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc");
#endif
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

static flux_rpc_t *flux_vrpcf (flux_t *h, const char *topic, uint32_t nodeid,
                               int flags, const char *fmt, va_list ap)
{
    flux_rpc_t *rpc;
    int rx_expected = 1;

    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(rpc = rpc_create (h, rx_expected)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
    cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    if (rpc_request_vsendf (rpc, topic, nodeid, fmt, ap) < 0)
        goto error;
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc.nodeid");
    cali_end_byname ("flux.message.rpc");
#endif
    return rpc;
error:
    flux_rpc_destroy (rpc);
    return NULL;
}

flux_rpc_t *flux_rpcf (flux_t *h, const char *topic, uint32_t nodeid,
                       int flags, const char *fmt, ...)
{
    va_list ap;
    flux_rpc_t *rpc;

    va_start (ap, fmt);
    rpc = flux_vrpcf (h, topic, nodeid, flags, fmt, ap);
    va_end (ap);

    return rpc;
}

flux_rpc_t *flux_rpc_multi (flux_t *h,
                            const char *topic,
                            const char *json_str,
                            const char *nodeset,
                            int flags)
{
    nodeset_t *ns = NULL;
    nodeset_iterator_t *itr = NULL;
    flux_rpc_t *rpc = NULL;
    int i;
    uint32_t count;
    int rx_expected;

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
    rx_expected = count;
    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(rpc = rpc_create (h, rx_expected)))
        goto error;
    if (!(itr = nodeset_iterator_create (ns)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "multi");
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    for (i = 0; i < count; i++) {
        uint32_t nodeid = nodeset_next (itr);
        assert (nodeid != NODESET_EOF);
#if HAVE_CALIPER
        cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
#endif
        if (rpc_request_send (rpc, topic, nodeid, json_str) < 0)
            goto error;
#if HAVE_CALIPER
        cali_end_byname ("flux.message.rpc.nodeid");
#endif
    }
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc");
#endif
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
    assert (rpc->magic == RPC_MAGIC);
    rpc->type = type;
}

void *flux_rpc_aux_get (flux_rpc_t *rpc)
{
    assert (rpc->magic == RPC_MAGIC);
    return rpc->aux;
}

void flux_rpc_aux_set (flux_rpc_t *rpc, void *aux, flux_free_f destroy)
{
    assert (rpc->magic == RPC_MAGIC);
    if (rpc->aux && rpc->aux_destroy)
        rpc->aux_destroy (rpc->aux);
    rpc->aux = aux;
    rpc->aux_destroy = destroy;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
