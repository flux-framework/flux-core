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

struct hello_struct {
    flux_t *h;
    attr_t *attrs;
    flux_msg_handler_t **handlers;
    uint32_t rank;
    uint32_t size;
    uint32_t count;

    double start;

    hello_cb_f cb;
    void *cb_arg;

    flux_reduce_t *reduce;
};

static void join_request (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg);

static void r_reduce (flux_reduce_t *r, int batch, void *arg);
static void r_sink (flux_reduce_t *r, int batch, void *arg);
static void r_forward (flux_reduce_t *r, int batch, void *arg);
static int r_itemweight (void *item);


struct flux_reduce_ops reduce_ops = {
    .destroy = NULL,
    .reduce = r_reduce,
    .sink = r_sink,
    .forward = r_forward,
    .itemweight = r_itemweight,
};

hello_t *hello_create (void)
{
    hello_t *hello = calloc (1, sizeof (*hello));

    if (!hello) {
        errno = ENOMEM;
        return NULL;
    }

    hello->size = 1;
    return hello;
}

void hello_destroy (hello_t *hello)
{
    if (hello) {
        flux_reduce_destroy (hello->reduce);
        flux_msg_handler_delvec (hello->handlers);
        free (hello);
    }
}

static int hwm_from_topology (attr_t *attrs)
{
    const char *s;
    if (attr_get (attrs, "tbon.descendants", &s, NULL) < 0) {
        log_err ("hello: reading tbon.descendants attribute");
        return 1;
    }
    return strtoul (s, NULL, 10) + 1;
}

int hello_register_attrs (hello_t *hello, attr_t *attrs)
{
    const char *s;
    double timeout = default_reduction_timeout;
    int hwm = hwm_from_topology (attrs);
    char num[32];

    hello->attrs = attrs;
    if (attr_get (attrs, "hello.timeout", &s, NULL) == 0) {
        if (fsd_parse_duration (s, &timeout) < 0) {
            log_err ("hello: invalid hello.timeout: %s", s);
            return -1;
        }
        if (attr_delete (attrs, "hello.timeout", true) < 0)
            return -1;
    }
    snprintf (num, sizeof (num), "%.3f", timeout);
    if (attr_add (attrs, "hello.timeout", num, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_get (attrs, "hello.hwm", &s, NULL) == 0) {
        hwm = strtoul (s, NULL, 10);
        if (attr_delete (attrs, "hello.hwm", true) < 0)
            return -1;
    }
    snprintf (num, sizeof (num), "%d", hwm);
    if (attr_add (attrs, "hello.hwm", num, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "hello.join",     join_request, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void hello_set_flux (hello_t *hello, flux_t *h)
{
    hello->h = h;
}

double hello_get_time (hello_t *hello)
{
    if (hello->start == 0. || hello->h == NULL)
        return 0.;
    return flux_reactor_now (flux_get_reactor (hello->h)) - hello->start;
}

int hello_get_count (hello_t *hello)
{
    return hello->count;
}

void hello_set_callback (hello_t *hello, hello_cb_f cb, void *arg)
{
    hello->cb = cb;
    hello->cb_arg = arg;
}

bool hello_complete (hello_t *hello)
{
    return (hello->size == hello->count);
}

int hello_start (hello_t *hello)
{
    int rc = -1;
    int flags = 0;
    int hwm = 1;
    double timeout = 0.;
    const char *s;

    if (flux_get_rank (hello->h, &hello->rank) < 0
                        || flux_get_size (hello->h, &hello->size) < 0) {
        log_err ("hello: error getting rank/size");
        goto done;
    }
    if (flux_msg_handler_addvec (hello->h, htab, hello,
                                 &hello->handlers) < 0) {
        log_err ("hello: adding message handlers");
        goto done;
    }
    if (hello->attrs) {
        if (attr_get (hello->attrs, "hello.hwm", &s, NULL) < 0) {
            log_err ("hello: reading hello.hwm attribute");
            goto done;
        }
        hwm = strtoul (s, NULL, 10);
        if (attr_get (hello->attrs, "hello.timeout", &s, NULL) < 0) {
            log_err ("hello: reading hello.timeout attribute");
            goto done;
        }
        if (fsd_parse_duration (s, &timeout) < 0)
            log_err ("hello: invalid hello.timeout attribute");
    }
    if (timeout > 0.)
        flags |= FLUX_REDUCE_TIMEDFLUSH;
    if (hwm > 0)
        flags |= FLUX_REDUCE_HWMFLUSH;
    if (!(hello->reduce = flux_reduce_create (hello->h, reduce_ops,
                                              timeout, hello, flags))) {
        log_err ("hello: creating reduction handle");
        goto done;
    }
    if (flux_reduce_opt_set (hello->reduce, FLUX_REDUCE_OPT_HWM,
                             &hwm, sizeof (hwm)) < 0) {
        log_err ("hello: setting FLUX_REDUCE_OPT_HWM");
        goto done;
    }

    flux_reactor_t *r = flux_get_reactor (hello->h);
    flux_reactor_now_update (r);
    hello->start = flux_reactor_now (r);
    if (flux_reduce_append (hello->reduce, (void *)(uintptr_t)1, 0) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

/* handle a message sent from downstream via downstream's r_forward op.
 */
static void join_request (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    hello_t *hello = arg;
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
    hello_t *hello = arg;
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
    hello_t *hello = arg;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
