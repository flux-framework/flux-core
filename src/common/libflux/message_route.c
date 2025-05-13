/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <flux/core.h>

#include "message.h"
#include "message_private.h"
#include "message_route.h"

static void route_id_destroy (void *data)
{
    if (data) {
        struct route_id *r = data;
        free (r);
    }
}

static struct route_id *route_id_create (const char *id, unsigned int id_len)
{
    struct route_id *r;
    if (!(r = calloc (1, sizeof (*r) + id_len + 1)))
        return NULL;
    r->id = (char *)(r + 1);
    if (id && id_len) {
        memcpy (r->id, id, id_len);
        list_node_init (&(r->route_id_node));
    }
    return r;
}

int msg_route_push (flux_msg_t *msg,
                    const char *id,
                    unsigned int id_len)
{
    struct route_id *r;
    if (!(r = route_id_create (id, strlen (id))))
        return -1;
    list_add (&msg->routes, &r->route_id_node);
    msg->routes_len++;
    return 0;
}

int msg_route_append (flux_msg_t *msg,
                      const char *id,
                      unsigned int id_len)
{
    struct route_id *r;
    assert (msg);
    assert (msg_has_route (msg));
    assert (id);
    if (!(r = route_id_create (id, id_len)))
        return -1;
    list_add_tail (&msg->routes, &r->route_id_node);
    msg->routes_len++;
    return 0;
}

void msg_route_clear (flux_msg_t *msg)
{
    struct route_id *r;
    assert (msg);
    assert (msg_has_route (msg));
    while ((r = list_pop (&msg->routes, struct route_id, route_id_node)))
        route_id_destroy (r);
    list_head_init (&msg->routes);
    msg->routes_len = 0;
}

int msg_route_delete_last (flux_msg_t *msg)
{
    struct route_id *r;
    assert (msg);
    assert (msg_has_route (msg));
    if ((r = list_pop (&msg->routes, struct route_id, route_id_node))) {
        route_id_destroy (r);
        msg->routes_len--;
    }
    return 0;
}

/* This function was added to streamline a critical path in the broker that
 * iteratively sends the same message to multiple peers.  It is equivalent to
 *   flux_msg_route_push (msg, id, strlen (id))
 *   cb (msg, arg)
 *   flux_msg_route_delete_last (msg)
 * but borrows the copy of id rather than copying it each time.
 */
int msg_route_sendto (const flux_msg_t *cmsg,
                      const char *id,
                      msg_route_send_f cb,
                      void *arg)
{
    flux_msg_t *msg = (flux_msg_t *)cmsg; // drop const since net effect is nil
    struct route_id r;
    int rc;

    list_node_init (&r.route_id_node);
    r.id = (char *)id;

    list_add (&msg->routes, &r.route_id_node);
    msg->routes_len++;

    rc = cb (msg, arg);

    list_pop (&msg->routes, struct route_id, route_id_node);
    msg->routes_len--;

    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

