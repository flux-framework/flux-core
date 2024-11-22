/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <flux/core.h>
#include <jansson.h>

#ifndef EBADE
#define EBADE EINVAL
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libccan/ccan/list/list.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libkvs/kvs_util_private.h"

#include "waitqueue.h"
#include "cache.h"

struct cache_entry {
    waitqueue_t *waitlist_notdirty;
    waitqueue_t *waitlist_valid;
    void *data;             /* value raw data */
    int len;
    json_t *o;              /* value treeobj object */
    double lastuse_time;    /* time of last use for cache expiry */
    bool valid;             /* flag indicating if raw data or treeobj
                             * set, don't use data == NULL as test, as
                             * zero length data can be valid */
    bool dirty;
    int errnum;
    char *blobref;
    int refcount;
    struct list_node entries_node;
    struct list_head *notdirty_list;
    struct list_node notdirty_node;
    struct list_head *valid_list;
    struct list_node valid_node;
};

struct cache {
    flux_reactor_t *r;
    double fake_time;       /* -1. for invalid */
    zhashx_t *zhx;
    /* entries_list is for fast iteration through entries, faster than
     * using zhashx iterators or zhashx_keys() */
    struct list_head entries_list;
    /* list of entries with notdirty & valid waitqueue's with messages
     * on them.  These lists are used to avoid excess iteration
     * through zhx */
    struct list_head notdirty_list;
    struct list_head valid_list;
};

static double cache_now (struct cache *cache)
{
    if (cache->fake_time >= 0.)
        return cache->fake_time;
    if (cache->r)
        return flux_reactor_now (cache->r);
    return 0.;
}

struct cache_entry *cache_entry_create (const char *ref)
{
    struct cache_entry *entry;

    if (!ref) {
        errno = EINVAL;
        return NULL;
    }

    if (!(entry = calloc (1, sizeof (*entry))))
        return NULL;

    if (!(entry->blobref = strdup (ref))) {
        cache_entry_destroy (entry);
        return NULL;
    }

    list_node_init (&entry->entries_node);
    list_node_init (&entry->notdirty_node);
    list_node_init (&entry->valid_node);
    return entry;
}

bool cache_entry_get_valid (struct cache_entry *entry)
{
    return (entry && entry->valid);
}

bool cache_entry_get_dirty (struct cache_entry *entry)
{
    return (entry && entry->valid && entry->dirty);
}

int cache_entry_set_dirty (struct cache_entry *entry, bool val)
{
    if (entry && entry->valid) {
        if ((val && entry->dirty) || (!val && !entry->dirty))
            ; /* no-op */
        else if (val && !entry->dirty)
            entry->dirty = true;
        else if (!val && entry->dirty) {
            entry->dirty = false;
            if (entry->waitlist_notdirty) {
                if (wait_runqueue (entry->waitlist_notdirty) < 0) {
                    /* set back dirty bit to orig */
                    entry->dirty = true;
                    return -1;
                }
                if (!wait_queue_msgs_count (entry->waitlist_notdirty))
                    list_del_init (&entry->notdirty_node);
            }
        }
        return 0;
    }
    return -1;
}

int cache_entry_clear_dirty (struct cache_entry *entry)
{
    if (entry && entry->valid) {
        if (entry->dirty
            && (!entry->waitlist_notdirty
                || !wait_queue_length (entry->waitlist_notdirty)))
            entry->dirty = false;
        return 0;
    }
    return -1;
}

int cache_entry_force_clear_dirty (struct cache_entry *entry)
{
    if (entry && entry->valid) {
        if (entry->dirty) {
            if (entry->waitlist_notdirty) {
                wait_queue_destroy (entry->waitlist_notdirty);
                list_del_init (&entry->notdirty_node);
                entry->waitlist_notdirty = NULL;
            }
            entry->dirty = false;
        }
        return 0;
    }
    return -1;
}

void cache_entry_incref (struct cache_entry *entry)
{
    if (entry)
        entry->refcount++;
}

void cache_entry_decref (struct cache_entry *entry)
{
    if (entry)
        entry->refcount--;
}

int cache_entry_get_raw (struct cache_entry *entry, const void **data,
                         int *len)
{
    if (!entry || !entry->valid)
        return -1;
    if (data)
        (*data) = entry->data;
    if (len)
        (*len) = entry->len;
    return 0;
}

int cache_entry_set_raw (struct cache_entry *entry, const void *data, int len)
{
    void *cpy = NULL;

    if (!entry || (data && len <= 0) || (!data && len)) {
        errno = EINVAL;
        return -1;
    }
    /* It should be a no-op if the entry is already set.
     * However, as a sanity check, make sure proposed and existing values match.
     */
    if (entry->valid) {
        if (len != entry->len || memcmp (data, entry->data, len) != 0) {
            errno = EBADE;
            return -1;
        }
        return 0;
    }
    if (len > 0) {
        if (!(cpy = malloc (len)))
            return -1;
        memcpy (cpy, data, len);
    }
    entry->data = cpy;
    entry->len = len;
    entry->valid = true;
    if (entry->waitlist_valid) {
        if (wait_runqueue (entry->waitlist_valid) < 0)
            goto reset_invalid;
        if (!wait_queue_msgs_count (entry->waitlist_valid))
            list_del_init (&entry->valid_node);
    }
    return 0;
reset_invalid:
    free (entry->data);
    entry->data = NULL;
    entry->len = 0;
    entry->valid = false;
    return -1;
}

static void set_wait_errnum (wait_t *w, void *arg)
{
    int *errnum = arg;
    (void)wait_aux_set_errnum (w, *errnum);
}

int cache_entry_set_errnum_on_valid (struct cache_entry *entry, int errnum)
{
    if (!entry || errnum <= 0) {
        errno = EINVAL;
        return -1;
    }

    entry->errnum = errnum;
    if (entry->waitlist_valid) {
        if (wait_queue_iter (entry->waitlist_valid,
                             set_wait_errnum,
                             &entry->errnum) < 0)
            return -1;
        if (wait_runqueue (entry->waitlist_valid) < 0)
            return -1;
        if (!wait_queue_msgs_count (entry->waitlist_valid))
            list_del_init (&entry->valid_node);
    }

    return 0;
}

int cache_entry_set_errnum_on_notdirty (struct cache_entry *entry, int errnum)
{
    if (!entry || errnum <= 0) {
        errno = EINVAL;
        return -1;
    }

    entry->errnum = errnum;
    if (entry->waitlist_notdirty) {
        if (wait_queue_iter (entry->waitlist_notdirty,
                             set_wait_errnum,
                             &entry->errnum) < 0)
            return -1;
        if (wait_runqueue (entry->waitlist_notdirty) < 0)
            return -1;
        if (!wait_queue_msgs_count (entry->waitlist_notdirty))
            list_del_init (&entry->notdirty_node);
    }

    return 0;
}

const json_t *cache_entry_get_treeobj (struct cache_entry *entry)
{
    if (!entry || !entry->valid || !entry->data)
        return NULL;
    if (!entry->o) {
        if (!(entry->o = treeobj_decodeb (entry->data, entry->len)))
            return NULL;
    }
    return entry->o;
}

void cache_entry_destroy (void *arg)
{
    struct cache_entry *entry = arg;
    if (entry) {
        int saved_errno = errno;
        free (entry->data);
        json_decref (entry->o);
        if (entry->waitlist_notdirty) {
            wait_queue_destroy (entry->waitlist_notdirty);
            list_del (&entry->notdirty_node);
        }
        if (entry->waitlist_valid) {
            wait_queue_destroy (entry->waitlist_valid);
            list_del (&entry->valid_node);
        }
        free (entry->blobref);
        free (entry);
        errno = saved_errno;
    }
}

/* ccan list doesn't appear to have a macro to check if a node exists
 * on a list */
static inline bool on_list (struct list_node *n)
{
    return !(n->next == n->prev && n->next == n);
}

int cache_entry_wait_notdirty (struct cache_entry *entry, wait_t *wait)
{
    if (wait) {
        if (!entry->waitlist_notdirty) {
            if (!(entry->waitlist_notdirty = wait_queue_create ()))
                return -1;
        }
        if (wait_addqueue (entry->waitlist_notdirty, wait) < 0)
            return -1;
        if (wait_queue_msgs_count (entry->waitlist_notdirty) > 0
            && entry->notdirty_list
            && !on_list (&entry->notdirty_node))
            list_add (entry->notdirty_list, &entry->notdirty_node);
    }
    return 0;
}

int cache_entry_wait_valid (struct cache_entry *entry, wait_t *wait)
{
    if (wait) {
        if (!entry->waitlist_valid) {
            if (!(entry->waitlist_valid = wait_queue_create ()))
                return -1;
        }
        if (wait_addqueue (entry->waitlist_valid, wait) < 0)
            return -1;
        if (wait_queue_msgs_count (entry->waitlist_valid) > 0
            && entry->valid_list
            && !on_list (&entry->valid_node))
            list_add (entry->valid_list, &entry->valid_node);
    }
    return 0;
}

struct cache_entry *cache_lookup (struct cache *cache, const char *ref)
{
    struct cache_entry *entry = zhashx_lookup (cache->zhx, ref);
    double current_time = cache_now (cache);
    if (entry && current_time > entry->lastuse_time)
        entry->lastuse_time = current_time;
    return entry;
}

int cache_insert (struct cache *cache, struct cache_entry *entry)
{
    __attribute__((unused)) int rc;

    if (cache && entry) {
        rc = zhashx_insert (cache->zhx, entry->blobref, entry);
        list_add (&cache->entries_list, &entry->entries_node);
        entry->notdirty_list = &cache->notdirty_list;
        entry->valid_list = &cache->valid_list;
        if (entry->waitlist_notdirty
            && wait_queue_msgs_count (entry->waitlist_notdirty) > 0)
            list_add (entry->notdirty_list, &entry->notdirty_node);
        if (entry->waitlist_valid
            && wait_queue_msgs_count (entry->waitlist_valid) > 0)
            list_add (entry->valid_list, &entry->valid_node);
        assert (rc == 0);
    }
    return 0;
}

int cache_remove_entry (struct cache *cache, const char *ref)
{
    struct cache_entry *entry = zhashx_lookup (cache->zhx, ref);

    if (entry
        && !entry->dirty
        && (!entry->waitlist_notdirty
            || !wait_queue_length (entry->waitlist_notdirty))
        && (!entry->waitlist_valid
            || !wait_queue_length (entry->waitlist_valid))) {
        list_del (&entry->entries_node);
        zhashx_delete (cache->zhx, ref);
        return 1;
    }
    return 0;
}

int cache_count_entries (struct cache *cache)
{
    return zhashx_size (cache->zhx);
}

static int cache_entry_age (struct cache_entry *entry, struct cache *cache)
{
    double current_time = cache_now (cache);
    if (!entry)
        return -1;
    if (entry->lastuse_time == 0.)
        entry->lastuse_time = current_time;
    return current_time - entry->lastuse_time;
}

int cache_expire_entries (struct cache *cache, double thresh)
{
    struct cache_entry *entry = NULL;
    struct cache_entry *next = NULL;
    int count = 0;

    list_for_each_safe (&cache->entries_list, entry, next, entries_node) {
        if (!cache_entry_get_dirty (entry)
            && cache_entry_get_valid (entry)
            && !entry->refcount
            && (thresh == 0. || cache_entry_age (entry, cache) > thresh)) {
                list_del (&entry->entries_node);
                zhashx_delete (cache->zhx, entry->blobref);
                count++;
        }
    }
    return count;
}

int cache_get_stats (struct cache *cache,
                     tstat_t *ts,
                     int *sizep,
                     int *incompletep,
                     int *dirtyp)
{
    struct cache_entry *entry;
    const char *key;
    int size = 0;
    int incomplete = 0;
    int dirty = 0;

    FOREACH_ZHASHX (cache->zhx, key, entry) {
        if (cache_entry_get_valid (entry)) {
            int obj_size = 0;

            if (entry->valid)
                obj_size = entry->len;

            size += obj_size;
            tstat_push (ts, obj_size);
        } else
            incomplete++;
        if (cache_entry_get_dirty (entry))
            dirty++;
    }
    if (sizep)
        *sizep = size;
    if (incompletep)
        *incompletep = incomplete;
    if (dirtyp)
        *dirtyp = dirty;
    return 0;
}

int cache_wait_destroy_msg (struct cache *cache, wait_test_msg_f cb, void *arg)
{
    struct cache_entry *entry = NULL;
    int n, count = 0;
    int rc = -1;

    list_for_each (&cache->notdirty_list, entry, notdirty_node) {
        assert (entry->waitlist_notdirty);
        if ((n = wait_destroy_msg (entry->waitlist_notdirty, cb, arg)) < 0)
            goto done;
        count += n;
    }
    list_for_each (&cache->valid_list, entry, valid_node) {
        assert (entry->waitlist_valid);
        if ((n = wait_destroy_msg (entry->waitlist_valid, cb, arg)) < 0)
            goto done;
        count += n;
    }
    rc = count;
done:
    return rc;
}

const char *cache_entry_get_blobref (struct cache_entry *entry)
{
    return entry ? entry->blobref : NULL;
}

static void cache_entry_destroy_wrapper (void **arg)
{
    if (arg) {
        cache_entry_destroy (*arg);
        *arg = NULL;
    }
}

struct cache *cache_create (flux_reactor_t *r)
{
    struct cache *cache = calloc (1, sizeof (*cache));
    if (!cache)
        return NULL;
    if (!(cache->zhx = zhashx_new ())) {
        free (cache);
        errno = ENOMEM;
        return NULL;
    }
    cache->r = r;
    cache->fake_time = -1.;
    /* do not duplicate hash keys, use blobrefs stored in cache entry */
    zhashx_set_key_destructor (cache->zhx, NULL);
    zhashx_set_key_duplicator (cache->zhx, NULL);
    zhashx_set_destructor (cache->zhx, cache_entry_destroy_wrapper);
    list_head_init (&cache->entries_list);
    list_head_init (&cache->notdirty_list);
    list_head_init (&cache->valid_list);
    return cache;
}

void cache_destroy (struct cache *cache)
{
    if (cache) {
        zhashx_destroy (&cache->zhx);
        free (cache);
    }
}

/* for testing */
void cache_entry_set_fake_time (struct cache_entry *entry, double time)
{
    if (entry)
        entry->lastuse_time = time;
}

/* for testing */
void cache_set_fake_time (struct cache *cache, double time)
{
    if (cache)
        cache->fake_time = time;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
