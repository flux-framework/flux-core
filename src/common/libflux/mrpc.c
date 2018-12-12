/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
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
#include "attr.h"
#include "reactor.h"
#include "msg_handler.h"
#include "mrpc.h"

#include "src/common/libidset/idset.h"
#include "src/common/libutil/aux.h"

#define MRPC_MAGIC 0x114422ae

struct flux_mrpc_struct {
    int magic;
    struct flux_match m;
    flux_t *h;
    flux_mrpc_continuation_f then_cb;
    void *then_arg;
    flux_msg_handler_t *mh;
    uint32_t nodeid;
    flux_msg_t *rx_msg;
    int rx_errnum;
    int rx_count;
    int rx_expected;
    struct aux_item *aux;
    flux_free_f aux_destroy;
    int usecount;
};

static void flux_mrpc_usecount_incr (flux_mrpc_t *mrpc)
{
    mrpc->usecount++;
}

static void flux_mrpc_usecount_decr (flux_mrpc_t *mrpc)
{
    if (!mrpc)
        return;
    assert (mrpc->magic == MRPC_MAGIC);
    if (--mrpc->usecount == 0) {
        if (mrpc->mh) {
            flux_msg_handler_stop (mrpc->mh);
            flux_msg_handler_destroy (mrpc->mh);
        }
        if (mrpc->m.matchtag != FLUX_MATCHTAG_NONE) {
            /* FIXME: we cannot safely return matchtags to the pool here
             * if the rpc was not completed.  Lacking a proper cancellation
             * protocol, we simply leak them.  See issue #212.
             */
            if (mrpc->rx_count >= mrpc->rx_expected)
                flux_matchtag_free (mrpc->h, mrpc->m.matchtag);
        }
        flux_msg_destroy (mrpc->rx_msg);
        aux_destroy (&mrpc->aux);
        mrpc->magic =~ MRPC_MAGIC;
        free (mrpc);
    }
}

void flux_mrpc_destroy (flux_mrpc_t *mrpc)
{
    int saved_errno = errno;
    flux_mrpc_usecount_decr (mrpc);
    errno = saved_errno;
}

static flux_mrpc_t *mrpc_create (flux_t *h, int rx_expected)
{
    flux_mrpc_t *mrpc;
    int flags = 0;

    if (!(mrpc = calloc (1, sizeof (*mrpc)))) {
        errno = ENOMEM;
        return NULL;
    }
    mrpc->magic = MRPC_MAGIC;
    if (rx_expected == 0) {
        mrpc->m.matchtag = FLUX_MATCHTAG_NONE;
    } else {
        if (rx_expected != 1)
            flags |= FLUX_MATCHTAG_GROUP;
        mrpc->m.matchtag = flux_matchtag_alloc (h, flags);
        if (mrpc->m.matchtag == FLUX_MATCHTAG_NONE) {
            flux_mrpc_destroy (mrpc);
            return NULL;
        }
    }
    mrpc->rx_expected = rx_expected;
    mrpc->m.typemask = FLUX_MSGTYPE_RESPONSE;
    mrpc->h = h;
    mrpc->usecount = 1;
    mrpc->nodeid = FLUX_NODEID_ANY;
    return mrpc;
}

static int mrpc_request_prepare (flux_mrpc_t *mrpc, flux_msg_t *msg,
                                uint32_t nodeid)
{
    int flags = 0;
    int rc = -1;
    uint32_t matchtag = mrpc->m.matchtag & ~FLUX_MATCHTAG_GROUP_MASK;
    uint32_t matchgrp = mrpc->m.matchtag & FLUX_MATCHTAG_GROUP_MASK;

    /* So that flux_mrpc_get_nodeid() can get nodeid from a response:
     * For group mrpc, use the lower matchtag bits to stash the nodeid
     * in the request, and it will come back in the response.
     * For singleton mrpc, stash destination nodeid in mrpc->nodeid.
     * TODO-scalability: If nodeids exceed 1<<20, we may need to use a seq#
     * in lower matchtag bits and stash a mapping array inthe mrpc object.
     */
    if (matchgrp > 0) {
        if ((nodeid & FLUX_MATCHTAG_GROUP_MASK) != 0) {
            errno = ERANGE;
            goto done;
        }
        matchtag = matchgrp | nodeid;
    } else
        mrpc->nodeid = nodeid;
    if (flux_msg_set_matchtag (msg, matchtag) < 0)
        goto done;
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        flags |= FLUX_MSGFLAG_UPSTREAM;
        if (flux_get_rank (mrpc->h, &nodeid) < 0)
            goto done;
    }
    if (flux_msg_set_nodeid (msg, nodeid, flags) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int mrpc_request_prepare_send (flux_mrpc_t *mrpc, flux_msg_t *msg,
                                     uint32_t nodeid)
{
    if (mrpc_request_prepare (mrpc, msg, nodeid) < 0)
        return -1;
    if (flux_send (mrpc->h, msg, 0) < 0)
        return -1;
    return 0;
}

bool flux_mrpc_check (flux_mrpc_t *mrpc)
{
    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc->rx_msg || mrpc->rx_errnum)
        return true;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
#endif
    if (!(mrpc->rx_msg = flux_recv (mrpc->h, mrpc->m, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            errno = 0;
        else
            mrpc->rx_errnum = errno;
    }
#if HAVE_CALIPER
    cali_end_byname ("flux.message.mrpc");
#endif
    return (mrpc->rx_msg || mrpc->rx_errnum);
}

static int mrpc_get (flux_mrpc_t *mrpc)
{
    int rc = -1;

    if (mrpc->rx_errnum) {
        errno = mrpc->rx_errnum;
        goto done;
    }
    if (!mrpc->rx_msg && !mrpc->rx_errnum) {
#if HAVE_CALIPER
        cali_begin_string_byname ("flux.message.rpc", "single");
#endif

        if (!(mrpc->rx_msg = flux_recv (mrpc->h, mrpc->m, 0)))
            mrpc->rx_errnum = errno;

#if HAVE_CALIPER
        cali_end_byname ("flux.message.rpc");
#endif

        if (!mrpc->rx_msg)
            goto done;

        mrpc->rx_count++;
    }
    rc = 0;
done:
    return rc;
}

int flux_mrpc_get (flux_mrpc_t *mrpc, const char **s)
{
    int rc = -1;

    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc_get (mrpc) < 0)
        goto done;
    if (flux_response_decode (mrpc->rx_msg, NULL, s) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_mrpc_get_raw (flux_mrpc_t *mrpc, const void **data, int *len)
{
    int rc = -1;

    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc_get (mrpc) < 0)
        goto done;
    if (flux_response_decode_raw (mrpc->rx_msg, NULL, data, len) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int flux_mrpc_vget_unpack (flux_mrpc_t *mrpc, const char *fmt, va_list ap)
{
    int rc = -1;

    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc_get (mrpc) < 0)
        goto done;
    if (flux_response_decode (mrpc->rx_msg, NULL, NULL) < 0)
        goto done;
    if (flux_msg_vunpack (mrpc->rx_msg, fmt, ap) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_mrpc_get_unpack (flux_mrpc_t *mrpc, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_mrpc_vget_unpack (mrpc, fmt, ap);
    va_end (ap);

    return rc;
}

int flux_mrpc_get_nodeid (flux_mrpc_t *mrpc, uint32_t *nodeid)
{
    int rc = -1;
    uint32_t tag;

    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc_get (mrpc) < 0)
        goto done;
    if (flux_msg_get_matchtag (mrpc->rx_msg, &tag) < 0)
        goto done;
    if ((tag & FLUX_MATCHTAG_GROUP_MASK) > 0)
        *nodeid = tag & ~FLUX_MATCHTAG_GROUP_MASK;
    else
        *nodeid = mrpc->nodeid;
    rc = 0;
done:
    return rc;
}

/* Internal callback for matching response.
 * For the multi-response case, overwrite previous message if
 * flux_mrpc_next () was not called.
 */
static void mrpc_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    flux_mrpc_t *mrpc = arg;
    assert (mrpc->then_cb != NULL);

    flux_mrpc_usecount_incr (mrpc);
    if (mrpc->rx_msg || mrpc->rx_errnum)
        (void)flux_mrpc_next (mrpc);
    if (!(mrpc->rx_msg = flux_msg_copy (msg, true)))
        goto done;
    mrpc->rx_count++;
    mrpc->then_cb (mrpc, mrpc->then_arg);
done:
    if (mrpc->rx_count >= mrpc->rx_expected || flux_fatality (mrpc->h))
        flux_msg_handler_stop (mrpc->mh);
    flux_mrpc_usecount_decr (mrpc);
}

int flux_mrpc_then (flux_mrpc_t *mrpc, flux_mrpc_continuation_f cb, void *arg)
{
    int rc = -1;

    assert (mrpc->magic == MRPC_MAGIC);
    if (mrpc->rx_count >= mrpc->rx_expected) {
        errno = EINVAL;
        goto done;
    }
    if (cb && !mrpc->then_cb) {
        if (!mrpc->mh) {
            if (!(mrpc->mh = flux_msg_handler_create (mrpc->h, mrpc->m,
                                                      mrpc_cb, mrpc)))
                goto done;
        }
        flux_msg_handler_start (mrpc->mh);
        if (mrpc->rx_msg || mrpc->rx_errnum) {
            if (mrpc->rx_msg)
                if (flux_requeue (mrpc->h, mrpc->rx_msg, FLUX_RQ_HEAD) < 0)
                    goto done;
            (void)flux_mrpc_next (mrpc);
        }
    } else if (!cb && mrpc->then_cb) {
        flux_msg_handler_stop (mrpc->mh);
    }
    mrpc->then_cb = cb;
    mrpc->then_arg = arg;
    rc = 0;
done:
    return rc;
}

int flux_mrpc_next (flux_mrpc_t *mrpc)
{
    assert (mrpc->magic == MRPC_MAGIC);
    if (flux_fatality (mrpc->h))
        return -1;
    if (mrpc->rx_count >= mrpc->rx_expected)
        return -1;
    flux_msg_destroy (mrpc->rx_msg);
    mrpc->rx_msg = NULL;
    mrpc->rx_errnum = 0;
    return 0;
}

static flux_mrpc_t *mrpc_request (flux_t *h,
                                  uint32_t nodeid,
                                  int flags,
                                  flux_msg_t *msg)
{
    flux_mrpc_t *mrpc;
    int rv, rx_expected = 1;

    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(mrpc = mrpc_create (h, rx_expected)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "single");
    cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    rv = mrpc_request_prepare_send (mrpc, msg, nodeid);
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc.nodeid");
    cali_end_byname ("flux.message.rpc");
#endif
    if (rv < 0)
        goto error;
    return mrpc;
error:
    flux_mrpc_destroy (mrpc);
    return NULL;
}

static flux_mrpc_t *mrpc (flux_t *h,
                          const char *nodeset,
                          int flags,
                          flux_msg_t *msg)
{
    struct idset *ns = NULL;
    flux_mrpc_t *mrpc = NULL;
    int rv = 0;
    uint32_t nodeid;
    uint32_t count;
    int rx_expected;

    if (!nodeset) {
        errno = EINVAL;
        goto error;
    }
    if (!strcmp (nodeset, "any"))
        return mrpc_request (h,
                             FLUX_NODEID_ANY,
                             flags,
                             msg);
    if (!strcmp (nodeset, "upstream"))
        return mrpc_request (h,
                             FLUX_NODEID_UPSTREAM,
                             flags,
                             msg);
    if (!strcmp (nodeset, "all")) {
        if (flux_get_size (h, &count) < 0)
            goto error;
        if (!(ns = idset_create (0, IDSET_FLAG_AUTOGROW)))
            goto error;
        if (idset_range_set (ns, 0, count - 1) < 0)
            goto error;
    } else {
        if (!(ns = idset_decode (nodeset)))
            goto error;
        count = idset_count (ns);
    }
    rx_expected = count;
    if ((flags & FLUX_RPC_NORESPONSE))
        rx_expected = 0;
    if (!(mrpc = mrpc_create (h, rx_expected)))
        goto error;
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.message.rpc", "multi");
    cali_begin_int_byname ("flux.message.response_expected",
                           !(flags & FLUX_RPC_NORESPONSE));
#endif
    nodeid = idset_first (ns);
    while (nodeid != IDSET_INVALID_ID) {
#if HAVE_CALIPER
        cali_begin_int_byname ("flux.message.rpc.nodeid", nodeid);
#endif
        rv = mrpc_request_prepare_send (mrpc, msg, nodeid);
#if HAVE_CALIPER
        cali_end_byname ("flux.message.rpc.nodeid");
#endif
        if (rv < 0)
            break;
        nodeid = idset_next (ns, nodeid);
    }
#if HAVE_CALIPER
    cali_end_byname ("flux.message.response_expected");
    cali_end_byname ("flux.message.rpc");
#endif
    if (rv < 0)
        goto error;
    idset_destroy (ns);
    return mrpc;
error:
    flux_mrpc_destroy (mrpc);
    idset_destroy (ns);
    return NULL;
}

flux_mrpc_t *flux_mrpc (flux_t *h,
                        const char *topic,
                        const char *s,
                        const char *nodeset,
                        int flags)
{
    flux_msg_t *msg;
    flux_mrpc_t *rc = NULL;

    if (!(msg = flux_request_encode (topic, s)))
        goto done;
    rc = mrpc (h, nodeset, flags, msg);
done:
    flux_msg_destroy (msg);
    return rc;
}

flux_mrpc_t *flux_mrpc_pack (flux_t *h,
                             const char *topic,
                             const char *nodeset,
                             int flags,
                             const char *fmt,
                             ...)
{
    flux_msg_t *msg;
    flux_mrpc_t *rc = NULL;
    va_list ap;

    va_start (ap, fmt);
    if (!(msg = flux_request_encode (topic, NULL)))
        goto done;
    if (flux_msg_vpack (msg, fmt, ap) < 0)
        goto done;
    rc = mrpc (h, nodeset, flags, msg);
done:
    va_end (ap);
    flux_msg_destroy (msg);
    return rc;
}

void *flux_mrpc_aux_get (flux_mrpc_t *mrpc, const char *name)
{
    if (!mrpc) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (mrpc->aux, name);
}

int flux_mrpc_aux_set (flux_mrpc_t *mrpc, const char *name,
                      void *aux, flux_free_f destroy)
{
    if (!mrpc) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&mrpc->aux, name, aux, destroy);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
