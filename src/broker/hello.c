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
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/monotime.h"

#include "heartbeat.h"
#include "hello.h"

struct hello_struct {
    nodeset_t nodeset;
    uint32_t count;
    double timeout;
    hello_cb_f cb;
    void *cb_arg;
    flux_t h;
    flux_timer_watcher_t *w;
    struct timespec start;
};


hello_t *hello_create (void)
{
    hello_t *hello = xzmalloc (sizeof (*hello));
    return hello;
}

void hello_destroy (hello_t *hello)
{
    if (hello) {
        if (hello->nodeset)
            nodeset_destroy (hello->nodeset);
        if (hello->w)
            flux_timer_watcher_destroy (hello->w);
        free (hello);
    }
}

void hello_set_flux (hello_t *hello, flux_t h)
{
    hello->h = h;
}

void hello_set_timeout (hello_t *hello, double seconds)
{
    hello->timeout = seconds;
}

double hello_get_time (hello_t *hello)
{
    if (!monotime_isset (hello->start))
        return 0;
    return monotime_since (hello->start) / 1000;
}

void hello_set_callback (hello_t *hello, hello_cb_f cb, void *arg)
{
    hello->cb = cb;
    hello->cb_arg = arg;
}

bool hello_complete (hello_t *hello)
{
    uint32_t size;
    if (flux_get_size (hello->h, &size) < 0)
        return false;
    return (size == hello->count);
}

const char *hello_get_nodeset (hello_t *hello)
{
    if (!hello->nodeset)
        return NULL;
    return nodeset_str (hello->nodeset);
}

static int hello_add_rank (hello_t *hello, uint32_t rank)
{
    uint32_t size;

    if (flux_get_size (hello->h, &size) < 0)
        return -1;
    if (!hello->nodeset)
        hello->nodeset = nodeset_new_size (size);
    if (!nodeset_add_rank (hello->nodeset, rank)) {
        errno = EPROTO;
        return -1;
    }
    hello->count++;
    if (hello->count == size) {
        if (hello->cb)
            hello->cb (hello, hello->cb_arg);
        if (hello->w)
            flux_timer_watcher_stop (hello->h, hello->w);
    }
    return 0;
}

int hello_recvmsg (hello_t *hello, const flux_msg_t *msg)
{
    int rc = -1;
    int rank;

    if (hello_decode (msg, &rank) < 0)
        goto done;
    if (hello_add_rank (hello, rank) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int hello_sendmsg (hello_t *hello, uint32_t rank)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = hello_encode (rank)))
        goto done;
    if (flux_send (hello->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static void timer_cb (flux_t h, flux_timer_watcher_t *w, int revents, void *arg)
{
    hello_t *hello = arg;

    if (hello->cb)
        hello->cb (hello, hello->cb_arg);
}

int hello_start (hello_t *hello)
{
    int rc = -1;
    uint32_t rank;

    if (flux_get_rank (hello->h, &rank) < 0)
        goto done;
    if (rank == 0) {
        monotime (&hello->start);
        if (!(hello->w = flux_timer_watcher_create (hello->timeout,
                                                    hello->timeout,
                                                    timer_cb, hello)))
            goto done;
        flux_timer_watcher_start (hello->h, hello->w);
        if (hello_add_rank (hello, 0) < 0)
            goto done;
    } else {
        if (hello_sendmsg (hello, rank) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

flux_msg_t *hello_encode (int rank)
{
    flux_msg_t *msg = NULL;
    JSON out = Jnew ();

    Jadd_int (out, "rank", rank);
    if (!(msg = flux_request_encode ("cmb.hello", Jtostr (out))))
        goto error;
    if (flux_msg_set_nodeid (msg, 0, 0) < 0)
        goto error;
    Jput (out);
    return msg;
error:
    flux_msg_destroy (msg);
    Jput (out);
    return NULL;
}

int hello_decode (const flux_msg_t *msg, int *rank)
{
    const char *json_str, *topic_str;
    JSON in = NULL;
    int rc = -1;

    if (flux_request_decode (msg, &topic_str, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "rank", rank)
                                    || strcmp (topic_str, "cmb.hello") != 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    Jput (in);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
