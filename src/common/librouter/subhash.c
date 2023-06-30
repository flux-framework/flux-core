/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* subhash.c - event subscriber registration hash
 *
 * Track subscriptions in a hash of topic strings.
 *
 * This class is designed so that a "router" can have a subhash
 * representing the combined subscriptions of all the router entries
 * (clients), and each router entry can have a subhash representing only
 * its subscriptions.
 *
 * The router entry (client) has its sh->sub() / sh->unsub() callbacks wired
 * to the router's subhash_subscribe() / subhash_unsubscribe() functions,
 * while the router's sh->sub() / sh->unsub() callbacks are wired to the
 * real flux_event_subscribe() / flux_event_unsubscribe().
 *
 * The first client to subscribe to a given topic triggers a
 * flux_event_subscribe(), while subsequent subscriptions from other clients
 * (to the same topic) increment the router's reference count.  Unsubscribes
 * decrement the router's reference count, while the last triggers a
 * flux_event_unsubscribe().
 *
 * subhash_topic_match() can be used to test if a message topic matches any
 * subscription topics for a given subhash, as an aid to event distribution.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "subhash.h"

struct subhash_entry {
    char *topic;
    int refcount;
    struct subhash *sh;
};

struct subhash {
    zhashx_t *subs;
    subscribe_f unsub;
    void *unsub_arg;
    subscribe_f sub;
    void *sub_arg;
};

/* N.B. sh->unsub() is only called in the entry destructor if the
 * subhash is being destroyed.  o/w subhash_unsubscribe() calls it
 * before calling zhashx_delete() to capture any error for the caller.
 */
static void subhash_entry_destroy (struct subhash_entry *entry)
{
    if (entry) {
        if (entry->sh && entry->sh->unsub)
            (void)entry->sh->unsub (entry->topic, entry->sh->unsub_arg);
        ERRNO_SAFE_WRAP (free, entry->topic);
        ERRNO_SAFE_WRAP (free, entry);
    }
}

// zhashx_destructor_fn footprint (wrapper)
static void subhash_entry_destructor (void **item)
{
    if (item) {
        subhash_entry_destroy (*item);
        *item = NULL;
    }
}

static struct subhash_entry *subhash_entry_create (const char *topic)
{
    struct subhash_entry *entry;

    if (!(entry = calloc (1, sizeof (*entry))))
        return NULL;
    if (!(entry->topic = strdup (topic)))
        goto error;
    return entry;
error:
    subhash_entry_destroy (entry);
    return NULL;
}

bool subhash_topic_match (struct subhash *sh, const char *topic)
{
    struct subhash_entry *entry;

    if (sh && topic) {
        entry = zhashx_first (sh->subs);
        while (entry) {
            /* entry->topic="" matches all
             * entry->topic="foo" matches "foo", "foobar", "foo.bar"
             */
            if (strstarts (topic, entry->topic))
                return true;
            entry = zhashx_next (sh->subs);
        }
    }
    return false;
}

int subhash_subscribe (struct subhash *sh, const char *topic)
{
    struct subhash_entry *entry;

    if (!sh || !topic) {
        errno = EINVAL;
        return -1;
    }
    if ((entry = zhashx_lookup (sh->subs, topic))) {
        entry->refcount++;
    }
    else {
        if (!(entry = subhash_entry_create (topic)))
            return -1;
        if (sh->sub) {
            if (sh->sub (topic, sh->sub_arg) < 0) {
                subhash_entry_destroy (entry);
                return -1;
            }
            entry->sh = sh;
        }
        entry->refcount = 1;
        zhashx_update (sh->subs, topic, entry);
    }
    return 0;
}

int subhash_unsubscribe (struct subhash *sh, const char *topic)
{
    struct subhash_entry *entry;

    if (!sh || !topic) {
        errno = EINVAL;
        return -1;
    }
    if ((entry = zhashx_lookup (sh->subs, topic))) {
        if (sh->unsub && entry->refcount == 1) {
            if (sh->unsub (topic, sh->unsub_arg) < 0)
                return -1;
            entry->sh = NULL; // prevent destructor from calling unsub()
        }
        if (--entry->refcount == 0)
            zhashx_delete (sh->subs, topic);
    }
    else {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

void subhash_set_subscribe (struct subhash *sh, subscribe_f cb, void *arg)
{
    if (sh) {
        sh->sub = cb;
        sh->sub_arg = arg;
    }
}

void subhash_set_unsubscribe (struct subhash *sh, subscribe_f cb, void *arg)
{
    if (sh) {
        sh->unsub = cb;
        sh->unsub_arg = arg;
    }
}

int subhash_renew (struct subhash *sh)
{
    struct subhash_entry *entry;

    if (sh) {
        entry = zhashx_first (sh->subs);
        while (entry) {
            if (sh->sub (entry->topic, sh->sub_arg) < 0)
                return -1;
            entry = zhashx_next (sh->subs);
        }
    }
    return 0;
}

void subhash_destroy (struct subhash *sh)
{
    if (sh) {
        ERRNO_SAFE_WRAP (zhashx_destroy, &sh->subs);
        ERRNO_SAFE_WRAP (free, sh);
    }
}

struct subhash *subhash_create (void)
{
    struct subhash *sh;

    if (!(sh = calloc (1, sizeof (*sh))))
        return NULL;
    if (!(sh->subs = zhashx_new ()))
        goto error;
    zhashx_set_destructor (sh->subs, subhash_entry_destructor);
    return sh;
error:
    subhash_destroy (sh);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
