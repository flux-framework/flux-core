/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "router.h"
#include "subhash.h"
#include "servhash.h"
#include "disconnect.h"

struct router_entry {
    char *uuid;
    router_entry_send_f send;
    void *arg;
    struct router *rtr;
    struct subhash *subscriptions;  // client's subscriber hash
    struct disconnect *dcon;
};

struct router {
    flux_t *h;
    zhashx_t *routes;               // uuid => 'struct router_entry'
    void *arg;
    struct subhash *subscriptions;  // router's subscriber hash
    struct servhash *services;
    flux_msg_handler_t **handlers;
    bool mute;
};

/* Generate internal response and 'msg' (success or failure only,
 * no payload) and send it to the client represented by 'entry'.
 */
static void router_entry_respond (struct router_entry *entry,
                                  const flux_msg_t *msg,
                                  int errnum)
{
    struct router *rtr = entry->rtr;
    const char *topic;
    flux_msg_t *rmsg;
    uint32_t matchtag;

    if (flux_msg_get_topic (msg, &topic) < 0)
        return;
    if (flux_msg_get_matchtag (msg, &matchtag) < 0)
        return;
    if (errnum)
        rmsg = flux_response_encode_error (topic, errnum, NULL);
    else
        rmsg = flux_response_encode (topic, NULL);
    if (!rmsg)
        goto done;
    if (flux_msg_set_rolemask (rmsg, FLUX_ROLE_OWNER) < 0)
        goto done;
    if (flux_msg_set_matchtag (rmsg, matchtag) < 0)
        goto done;
    if (entry->send (rmsg, entry->arg) < 0) {
        if (errno != EPIPE && errnum != ECONNRESET) {
            flux_log_error (rtr->h,
                            "router: response > client=%.5s",
                            entry->uuid);
        }
        goto done;
    }
done:
    flux_msg_destroy (rmsg);
}

/* servhash respond_f footprint */
static void router_entry_respond_byuuid (const flux_msg_t *msg,
                                         const char *uuid,
                                         int errnum,
                                         void *arg)
{
    struct router *rtr = arg;
    struct router_entry *entry;

    if ((entry = zhashx_lookup (rtr->routes, uuid)))
        router_entry_respond (entry, msg, errnum);
}

/* Handle internal local subscribe request.
 */
static void local_sub_request (struct router_entry *entry, flux_msg_t *msg)
{
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{s:s}", "topic", &topic) < 0)
        goto error;
    if (subhash_subscribe (entry->subscriptions, topic) < 0)
        goto error;
    router_entry_respond (entry, msg, 0);
    return;
error:
    router_entry_respond (entry, msg, errno);
}

/* Handle internal local unsubscribe request.
 */
static void local_unsub_request (struct router_entry *entry, flux_msg_t *msg)
{
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{s:s}", "topic", &topic) < 0)
        goto error;
    if (subhash_unsubscribe (entry->subscriptions, topic) < 0)
        goto error;
    router_entry_respond (entry, msg, 0);
    return;
error:
    router_entry_respond (entry, msg, errno);
}

/* Handle internal service.add request.
 */
static void service_add_request (struct router_entry *entry, flux_msg_t *msg)
{
    struct router *rtr = entry->rtr;
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "service", &name) < 0)
        goto error;
    if (servhash_add (rtr->services, name, entry->uuid, msg) < 0)
        goto error;
    return;
error:
    router_entry_respond (entry, msg, errno);
}

/* Handle internal service.remove request.
 */
static void service_remove_request (struct router_entry *entry, flux_msg_t *msg)
{
    struct router *rtr = entry->rtr;
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "service", &name) < 0)
        goto error;
    if (servhash_remove (rtr->services, name, entry->uuid, msg) < 0)
        goto error;
    return;
error:
    router_entry_respond (entry, msg, errno);
}

/* Receive a message from a client represented by 'entry'.
 * Most messages will be forwarded to the broker as is, but some
 * require conditioning, and some requests are handled internally.
 */
void router_entry_recv (struct router_entry *entry, flux_msg_t *msg)
{
    struct router *rtr = entry->rtr;
    int type;
    const char *topic;

    if (flux_msg_get_type (msg, &type) < 0)
        return;
    if (flux_msg_get_topic (msg, &topic) < 0)
        return;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (streq (topic, "event.subscribe")) {
                local_sub_request (entry, msg);
                break;
            }
            if (streq (topic, "event.unsubscribe")) {
                local_unsub_request (entry, msg);
                break;
            }
            if (streq (topic, "service.add")) {
                service_add_request (entry, msg);
                break;
            }
            if (streq (topic, "service.remove")) {
                service_remove_request (entry, msg);
                break;
            }
            flux_msg_route_enable (msg);
            if (flux_msg_route_push (msg, entry->uuid) < 0)
                return;
            if (disconnect_arm (entry->dcon, msg) < 0)
                return;
            /* fall through */
        case FLUX_MSGTYPE_EVENT:
        case FLUX_MSGTYPE_RESPONSE:
            if (flux_send (rtr->h, msg, 0) < 0) {
                flux_log_error (rtr->h,
                                "router: client=%.5s %s %s > broker",
                                entry->uuid,
                                flux_msg_typestr (type),
                                topic);
                return;
            }
    }
}

static int broker_subscribe (const char *topic, void *arg)
{
    struct router *rtr = arg;

    if (flux_event_subscribe (rtr->h, topic) < 0)
        return -1;
    return 0;
}

static int broker_unsubscribe (const char *topic, void *arg)
{
    struct router *rtr = arg;

    if (!rtr->mute && flux_event_unsubscribe (rtr->h, topic) < 0)
        return -1;
    return 0;
}

/* A client asks the router to subscribe.
 * This might generate a broker_subscribe() or just usecount++.
 */
static int router_subscribe (const char *topic, void *arg)
{
    struct router *rtr = arg;

    return subhash_subscribe (rtr->subscriptions, topic);
}

/* A client asks the router to unsubscribe.
 * This might generate a broker_unsubscribe() or just usecount--.
 */
static int router_unsubscribe (const char *topic, void *arg)
{
    struct router *rtr = arg;

    return subhash_unsubscribe (rtr->subscriptions, topic);
}

static void disconnect_cb (const flux_msg_t *msg, void *arg)
{
    struct router_entry *entry = arg;
    struct router *rtr = entry->rtr;

    if (rtr && rtr->h) {
        if (flux_send (rtr->h, msg, 0) < 0) {
            flux_log_error (rtr->h,
                            "router: disconnect < client=%.5s",
                            entry->uuid);
        }
    }
}

static void router_entry_destroy (struct router_entry *entry)
{
    if (entry) {
        struct router *rtr = entry->rtr;

        disconnect_destroy (entry->dcon);
        servhash_disconnect (rtr->services, entry->uuid);
        subhash_destroy (entry->subscriptions);
        ERRNO_SAFE_WRAP (free, entry->uuid);
        ERRNO_SAFE_WRAP (free, entry);
    }
}

// zhashx_destructor_fn footprint (wrapper)
static void router_entry_destructor (void **item)
{
    if (item) {
        router_entry_destroy (*item);
        *item = NULL;
    }
}

static struct router_entry *router_entry_create (const char *uuid,
                                                 router_entry_send_f cb,
                                                 void *arg)
{
    struct router_entry *entry;

    if (!uuid || !cb) {
        errno = EINVAL;
        return NULL;
    }
    if (!(entry = calloc (1, sizeof (*entry))))
        return NULL;
    if (!(entry->uuid = strdup (uuid)))
        goto error;
    if (!(entry->subscriptions = subhash_create ()))
        goto error;

    if (!(entry->dcon = disconnect_create (disconnect_cb, entry)))
        goto error;
    entry->send = cb;
    entry->arg = arg;
    return entry;
error:
    router_entry_destroy (entry);
    return NULL;
}

struct router_entry *router_entry_add (struct router *rtr,
                                       const char *uuid,
                                       router_entry_send_f cb,
                                       void *arg)
{
    struct router_entry *entry;

    if (!(entry = router_entry_create (uuid, cb, arg)))
        return NULL;

    subhash_set_subscribe (entry->subscriptions, router_subscribe, rtr);
    subhash_set_unsubscribe (entry->subscriptions, router_unsubscribe, rtr);

    if (zhashx_insert (rtr->routes, uuid, entry) < 0) {
        router_entry_destroy (entry);
        errno = EEXIST;
        return NULL;
    }
    entry->rtr = rtr;
    return entry;
}

void router_entry_delete (struct router_entry *entry)
{
    if (entry && entry->rtr && entry->rtr->routes)
        zhashx_delete (entry->rtr->routes, entry->uuid);
}

/* Receive request from broker.
 * Forward to client with registered service or respond with ENOSYS.
 */
static void request_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct router *rtr = arg;
    struct router_entry *entry;
    const char *uuid;

    if (servhash_match (rtr->services, msg, &uuid) < 0)
        goto error;
    if (!(entry = zhashx_lookup (rtr->routes, uuid)))
        goto error;
    if (entry->send (msg, entry->arg) < 0) {
        if (errno != EPIPE && errno != EWOULDBLOCK)
            flux_log_error (h, "router: request > client=%.5s", entry->uuid);
    }
    return;
error:
    if (flux_respond_error (h, msg, ENOSYS, NULL) < 0)
        flux_log_error (h, "router: request > client");
}

/* Receive response from broker.
 * Pop uuid off route stack, lookup uuid key in rtr->routes hash,
 * and send to resulting client.
 */
static void response_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct router *rtr = arg;
    struct router_entry *entry = NULL;
    flux_msg_t *cpy;
    const char *uuid = NULL;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto error;
    if (!(uuid = flux_msg_route_last (cpy))) { // may set uuid=NULL no routes
        errno = EINVAL;
        goto error;
    }
    if (!(entry = zhashx_lookup (rtr->routes, uuid))) {
        errno = EHOSTUNREACH;
        goto error;
    }
    if (flux_msg_route_delete_last (cpy) < 0)
        goto error;
    if (entry->send (cpy, entry->arg) < 0) {
        flux_log_error (h, "router: response > client=%.5s", entry->uuid);
        goto error;
    }
error:
    flux_msg_destroy (cpy);
    return;
}

/* Receive event from broker.
 * Distribute to all router entries with matching subscriptions.
 */
static void event_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct router *rtr = arg;
    struct router_entry *entry;
    const char *topic;

    if (flux_msg_get_topic (msg, &topic) < 0) {
        flux_log_error (h, "router: event > client");
        return;
    }

    entry = zhashx_first (rtr->routes);
    while ((entry)) {
        if (subhash_topic_match (entry->subscriptions, topic)) {
            if (entry->send (msg, entry->arg) < 0)
                flux_log_error (h, "router: event > client=%.5s", entry->uuid);
        }
        entry = zhashx_next (rtr->routes);
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     NULL, event_cb,    FLUX_ROLE_ALL },
    { FLUX_MSGTYPE_RESPONSE,  NULL, response_cb, FLUX_ROLE_ALL },
    { FLUX_MSGTYPE_REQUEST,   NULL, request_cb,  FLUX_ROLE_ALL },
    FLUX_MSGHANDLER_TABLE_END
};

struct router *router_create (flux_t *h)
{
    struct router *rtr;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(rtr = calloc (1, sizeof (*rtr))))
        return NULL;
    rtr->h = h;

    if (!(rtr->routes = zhashx_new ()))
        goto error;
    zhashx_set_destructor (rtr->routes, router_entry_destructor);

    if (!(rtr->subscriptions = subhash_create ()))
        goto error;
    subhash_set_subscribe (rtr->subscriptions, broker_subscribe, rtr);
    subhash_set_unsubscribe (rtr->subscriptions, broker_unsubscribe, rtr);

    if (!(rtr->services = servhash_create (h)))
        goto error;
    servhash_set_respond (rtr->services, router_entry_respond_byuuid, rtr);

    if (flux_msg_handler_addvec (h, htab, rtr, &rtr->handlers) < 0)
        goto error;
    return rtr;
error:
    router_destroy (rtr);
    return NULL;
}

void router_destroy (struct router *rtr)
{
    if (rtr) {
        flux_msg_handler_delvec (rtr->handlers);
        subhash_destroy (rtr->subscriptions);
        servhash_destroy (rtr->services);
        ERRNO_SAFE_WRAP (zhashx_destroy, &rtr->routes);
        ERRNO_SAFE_WRAP (free, rtr);
    }
}

void router_mute (struct router *rtr)
{
    if (rtr)
        rtr->mute = true;
}

int router_renew (struct router *rtr)
{
    if (rtr) {
        if (subhash_renew (rtr->subscriptions) < 0)
            return -1;
        if (servhash_renew (rtr->services) < 0)
            return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
