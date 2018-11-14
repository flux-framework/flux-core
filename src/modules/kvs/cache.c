/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libkvs/treeobj.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"

#include "waitqueue.h"
#include "cache.h"
#include "kvs_util.h"

struct cache_entry {
    waitqueue_t *waitlist_notdirty;
    waitqueue_t *waitlist_valid;
    void *data;             /* value raw data */
    int len;
    json_t *o;              /* value treeobj object */
    int lastuse_epoch;      /* time of last use for cache expiry */
    bool valid;             /* flag indicating if raw data or treeobj
                             * set, don't use data == NULL as test, as
                             * zero length data can be valid */
    bool dirty;
    int errnum;
    char *blobref;
};

struct cache {
    zhash_t *zh;
};

struct cache_entry *cache_entry_create (const char *ref)
{
    struct cache_entry *entry;

    if (!ref) {
        errno = EINVAL;
        return NULL;
    }

    if (!(entry = calloc (1, sizeof (*entry)))) {
        errno = ENOMEM;
        return NULL;
    }

    if (!(entry->blobref = strdup (ref))) {
        cache_entry_destroy (entry);
        errno = ENOMEM;
        return NULL;
    }

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
                entry->waitlist_notdirty = NULL;
            }
            entry->dirty = false;
        }
        return 0;
    }
    return -1;
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
        free (entry->data);
        json_decref (entry->o);
        if (entry->waitlist_notdirty)
            wait_queue_destroy (entry->waitlist_notdirty);
        if (entry->waitlist_valid)
            wait_queue_destroy (entry->waitlist_valid);
        free (entry->blobref);
        free (entry);
    }
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
    }
    return 0;
}

struct cache_entry *cache_lookup (struct cache *cache, const char *ref,
                                  int current_epoch)
{
    struct cache_entry *entry = zhash_lookup (cache->zh, ref);
    if (entry && current_epoch > entry->lastuse_epoch)
        entry->lastuse_epoch = current_epoch;
    return entry;
}

int cache_insert (struct cache *cache, struct cache_entry *entry)
{
    int rc;

    if (cache && entry) {
        rc = zhash_insert (cache->zh, entry->blobref, entry);
        assert (rc == 0);

        zhash_freefn (cache->zh, entry->blobref, cache_entry_destroy);
    }
    return 0;
}

int cache_remove_entry (struct cache *cache, const char *ref)
{
    struct cache_entry *entry = zhash_lookup (cache->zh, ref);

    if (entry
        && !entry->dirty
        && (!entry->waitlist_notdirty
            || !wait_queue_length (entry->waitlist_notdirty))
        && (!entry->waitlist_valid
            || !wait_queue_length (entry->waitlist_valid))) {
        zhash_delete (cache->zh, ref);
        return 1;
    }
    return 0;
}

int cache_count_entries (struct cache *cache)
{
    return zhash_size (cache->zh);
}

static int cache_entry_age (struct cache_entry *entry, int current_epoch)
{
    if (!entry)
        return -1;
    if (entry->lastuse_epoch == 0)
        entry->lastuse_epoch = current_epoch;
    return current_epoch - entry->lastuse_epoch;
}

int cache_expire_entries (struct cache *cache, int current_epoch, int thresh)
{
    zlist_t *keys;
    char *ref;
    struct cache_entry *entry;
    int count = 0;

    if (!(keys = zhash_keys (cache->zh))) {
        errno = ENOMEM;
        return -1;
    }
    while ((ref = zlist_pop (keys))) {
        if ((entry = zhash_lookup (cache->zh, ref))
            && !cache_entry_get_dirty (entry)
            && cache_entry_get_valid (entry)
            && (thresh == 0
                || cache_entry_age (entry, current_epoch) > thresh)) {
                zhash_delete (cache->zh, ref);
                count++;
        }
        free (ref);
    }
    zlist_destroy (&keys);
    return count;
}

int cache_get_stats (struct cache *cache, tstat_t *ts, int *sizep,
                     int *incompletep, int *dirtyp)
{
    zlist_t *keys = NULL;
    struct cache_entry *entry;
    char *ref;
    int size = 0;
    int incomplete = 0;
    int dirty = 0;
    int saved_errno;
    int rc = -1;

    if (!(keys = zhash_keys (cache->zh))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }
    while ((ref = zlist_pop (keys))) {
        entry = zhash_lookup (cache->zh, ref);
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
        free (ref);
    }
    if (sizep)
        *sizep = size;
    if (incompletep)
        *incompletep = incomplete;
    if (dirtyp)
        *dirtyp = dirty;
    rc = 0;
cleanup:
    zlist_destroy (&keys);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

int cache_wait_destroy_msg (struct cache *cache, wait_test_msg_f cb, void *arg)
{
    const char *key;
    struct cache_entry *entry;
    int n, count = 0;
    int rc = -1;

    FOREACH_ZHASH (cache->zh, key, entry) {
        if (entry->waitlist_valid) {
            if ((n = wait_destroy_msg (entry->waitlist_valid, cb, arg)) < 0)
                goto done;
            count += n;
        }
        if (entry->waitlist_notdirty) {
            if ((n = wait_destroy_msg (entry->waitlist_notdirty, cb, arg)) < 0)
                goto done;
            count += n;
        }
    }
    rc = count;
done:
    return rc;
}

const char *cache_entry_get_blobref (struct cache_entry *entry)
{
    return entry ? entry->blobref : NULL;
}

struct cache *cache_create (void)
{
    struct cache *cache = calloc (1, sizeof (*cache));
    if (!cache) {
        errno = ENOMEM;
        return NULL;
    }
    if (!(cache->zh = zhash_new ())) {
        free (cache);
        errno = ENOMEM;
        return NULL;
    }
    return cache;
}

void cache_destroy (struct cache *cache)
{
    if (cache) {
        zhash_destroy (&cache->zh);
        free (cache);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
