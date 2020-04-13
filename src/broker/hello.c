/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"

#include "hello.h"
#include "reduce.h"

/* After this many seconds, ignore topo-based hwm.
 * Override by setting hello.timeout broker attribute.
 */
static double default_reduction_timeout = 10.;

struct hello {
    flux_t *h;
    flux_msg_handler_t **handlers;
    uint32_t rank;
    uint32_t size;
    uint32_t count;

    double start;

    hello_cb_f cb;
    void *cb_arg;

    flux_reduce_t *reduce;
};

double hello_get_time (struct hello *hello)
{
    if (hello->start == 0. || hello->h == NULL)
        return 0.;
    return flux_reactor_now (flux_get_reactor (hello->h)) - hello->start;
}

int hello_get_count (struct hello *hello)
{
    return hello->count;
}

bool hello_complete (struct hello *hello)
{
    return (hello->size == hello->count);
}

int hello_start (struct hello *hello)
{
    flux_reactor_t *r = flux_get_reactor (hello->h);
    flux_reactor_now_update (r);
    hello->start = flux_reactor_now (r);
    if (flux_reduce_append (hello->reduce, (void *)(uintptr_t)1, 0) < 0)
        return -1;
    return 0;
}

/* handle a message sent from downstream via downstream's r_forward op.
 */
static void join_request (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct hello *hello = arg;
    int count, batch;

    if (flux_request_unpack (msg, NULL, "{ s:i s:i }",
                             "count", &count,
                             "batch", &batch) < 0)
        log_err_exit ("hello: flux_request_unpack");
    if (batch != 0 || count <= 0)
        log_msg_exit ("hello: error decoding join request");
    if (flux_reduce_append (hello->reduce, (void *)(uintptr_t)count, batch) < 0)
        log_err_exit ("hello: flux_reduce_append");
}

/* Reduction ops
 * N.B. since we are reducing integers, we cheat and avoid memory
 * allocation by stuffing the int inside the pointer value.
 */

/* Pop one or more counts, push their sum
 */
static void r_reduce (flux_reduce_t *r, int batch, void *arg)
{
    int i, count = 0;

    assert (batch == 0);

    while ((i = (uintptr_t)flux_reduce_pop (r)) > 0)
        count += i;
    if (count > 0 && flux_reduce_push (r, (void *)(uintptr_t)count) < 0)
        log_err_exit ("hello: flux_reduce_push");
    /* Invariant for r_sink and r_forward:
     * after reduce, handle contains exactly one item.
     */
}

/* (called on rank 0 only) Pop exactly one count, update global count,
 * call the registered callback.
 * This may be called once the total hwm is reached on rank 0,
 * or after the timeout, as new messages arrive (after r_reduce).
 */
static void r_sink (flux_reduce_t *r, int batch, void *arg)
{
    struct hello *hello = arg;
    int count = (uintptr_t)flux_reduce_pop (r);

    assert (batch == 0);
    assert (count > 0);

    hello->count += count;
    if (hello->cb)
        hello->cb (hello, hello->cb_arg);
}

/* (called on rank > 0 only) Pop exactly one count, forward upstream.
 * This may be called once the hwm is reached on this rank (based on topo),
 * or after the timeout, as new messages arrive (after r_reduce).
 */
static void r_forward (flux_reduce_t *r, int batch, void *arg)
{
    flux_future_t *f;
    struct hello *hello = arg;
    int count = (uintptr_t)flux_reduce_pop (r);

    assert (batch == 0);
    assert (count > 0);

    if (!(f = flux_rpc_pack (hello->h, "hello.join", FLUX_NODEID_UPSTREAM,
                             FLUX_RPC_NORESPONSE, "{ s:i s:i }",
                             "count", count,
                             "batch", batch)))
        log_err_exit ("hello: flux_rpc_pack");
    flux_future_destroy (f);
}

/* How many original items does this item represent after reduction?
 * In this simple case it is just the value of the item (the count).
 */
static int r_itemweight (void *item)
{
    return (uintptr_t)item;
}

struct flux_reduce_ops reduce_ops = {
    .destroy = NULL,
    .reduce = r_reduce,
    .sink = r_sink,
    .forward = r_forward,
    .itemweight = r_itemweight,
};

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "hello.join",     join_request, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct hello *hello_create (flux_t *h, attr_t *attrs, hello_cb_f cb, void *arg)
{
    struct hello *hello;
    double timeout = default_reduction_timeout;
    int hwm = 1;
    int flags = 0; // reduction flags

    if (!(hello = calloc (1, sizeof (*hello))))
        return NULL;

    hello->h = h;
    hello->size = 1;
    hello->cb = cb;
    hello->cb_arg = arg;

    if (flux_msg_handler_addvec (hello->h, htab, hello, &hello->handlers) < 0)
        goto error;

    if (flux_get_rank (hello->h, &hello->rank) < 0)
        goto error;
    if (flux_get_size (hello->h, &hello->size) < 0)
        goto error;

    if (attrs) {
        const char *s;
        char num[32];

        /* hello.hwm
         * Consider hello data all collected once data from 'hwm' nodes
         * is available (TBON descendants plus self).
         */
        if (attr_get (attrs, "tbon.descendants", &s, NULL) < 0) {
            log_err ("hello: reading tbon.descendants attribute");
            goto error;
        }
        hwm = strtoul (s, NULL, 10) + 1;
        snprintf (num, sizeof (num), "%d", hwm);
        if (attr_add (attrs, "hello.hwm", num, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto error;

        /* hello.timeout (tunable)
         * If timeout expires before from 'hwm' nodes is available,
         * send what is available so far upstream.
         */
        if (attr_get (attrs, "hello.timeout", &s, NULL) == 0) {
            if (fsd_parse_duration (s, &timeout) < 0) {
                log_err ("hello: invalid hello.timeout: %s", s);
                goto error;
            }
            if (attr_set_flags (attrs,
                                "hello.timeout",
                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
                goto error;
        }
        else {
            snprintf (num, sizeof (num), "%.3f", timeout);
            if (attr_add (attrs, "hello.timeout", num, FLUX_ATTRFLAG_IMMUTABLE) < 0)
                goto error;
        }
    }

    /* Create the reduction handle for this broker.
     */
    if (hwm > 0)
        flags |= FLUX_REDUCE_HWMFLUSH;
    if (timeout > 0.)
        flags |= FLUX_REDUCE_TIMEDFLUSH;

    if (!(hello->reduce = flux_reduce_create (hello->h,
                                              reduce_ops,
                                              timeout,
                                              hello,
                                              flags)))
        goto error;
    if (flux_reduce_opt_set (hello->reduce,
                             FLUX_REDUCE_OPT_HWM,
                             &hwm,
                             sizeof (hwm)) < 0)
        goto error;

    return hello;
error:
    hello_destroy (hello);
    return NULL;
}

void hello_destroy (struct hello *hello)
{
    if (hello) {
        int saved_errno = errno;
        flux_reduce_destroy (hello->reduce);
        flux_msg_handler_delvec (hello->handlers);
        free (hello);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
