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

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/oom.h"

#include "waitqueue.h"
#include "cache.h"

struct cache_entry {
    waitqueue_t *waitlist_notdirty;
    waitqueue_t *waitlist_valid;
    json_t *o;              /* value object */
    int lastuse_epoch;      /* time of last use for cache expiry */
    uint8_t dirty:1;
};

struct cache {
    zhash_t *zh;
};

struct cache_entry *cache_entry_create (json_t *o)
{
    struct cache_entry *hp = xzmalloc (sizeof (*hp));
    if (o)
        hp->o = o;
    return hp;
}

bool cache_entry_get_valid (struct cache_entry *hp)
{
    return (hp && hp->o != NULL);
}

bool cache_entry_get_dirty (struct cache_entry *hp)
{
    return (hp && hp->o && hp->dirty);
}

void cache_entry_set_dirty (struct cache_entry *hp, bool val)
{
    if (hp && hp->o) {
        if ((val && hp->dirty) || (!val && !hp->dirty))
            ; /* no-op */
        else if (val && !hp->dirty)
            hp->dirty = 1;
        else if (!val && hp->dirty) {
            hp->dirty = 0;
            if (hp->waitlist_notdirty)
                wait_runqueue (hp->waitlist_notdirty);
        }
    }
}

json_t *cache_entry_get_json (struct cache_entry *hp)
{
    if (!hp || !hp->o)
        return NULL;
    return hp->o;
}

void cache_entry_set_json (struct cache_entry *hp, json_t *o)
{
    if (hp) {
        if ((o && hp->o) || (!o && !hp->o)) {
            json_decref (o); /* no-op, 'o' is assumed identical to hp->o */
        } else if (o && !hp->o) {
            hp->o = o;
            if (hp->waitlist_valid)
                wait_runqueue (hp->waitlist_valid);
        } else if (!o && hp->o) {
            json_decref (hp->o);
            hp->o = NULL;
        }
    }
}

void cache_entry_destroy (void *arg)
{
    struct cache_entry *hp = arg;
    if (hp) {
        if (hp->o)
            json_decref (hp->o);
        if (hp->waitlist_notdirty)
            wait_queue_destroy (hp->waitlist_notdirty);
        if (hp->waitlist_valid)
            wait_queue_destroy (hp->waitlist_valid);
        free (hp);
    }
}

void cache_entry_wait_notdirty (struct cache_entry *hp, wait_t *wait)
{
    if (wait) {
        if (!hp->waitlist_notdirty)
            hp->waitlist_notdirty = wait_queue_create ();
        wait_addqueue (hp->waitlist_notdirty, wait);
    }
}

void cache_entry_wait_valid (struct cache_entry *hp, wait_t *wait)
{
    if (wait) {
        if (!hp->waitlist_valid)
            hp->waitlist_valid = wait_queue_create ();
        wait_addqueue (hp->waitlist_valid, wait);
    }
}

struct cache_entry *cache_lookup (struct cache *cache, const char *ref,
                                  int current_epoch)
{
    struct cache_entry *hp = zhash_lookup (cache->zh, ref);
    if (hp && current_epoch > hp->lastuse_epoch)
        hp->lastuse_epoch = current_epoch;
    return hp;
}

json_t *cache_lookup_and_get_json (struct cache *cache,
                                   const char *ref,
                                   int current_epoch)
{
    struct cache_entry *hp = cache_lookup (cache, ref, current_epoch);
    return cache_entry_get_valid (hp) ? cache_entry_get_json (hp) : NULL;
}

void cache_insert (struct cache *cache, const char *ref, struct cache_entry *hp)
{
    int rc = zhash_insert (cache->zh, ref, hp);
    assert (rc == 0);
    zhash_freefn (cache->zh, ref, cache_entry_destroy);
}

int cache_count_entries (struct cache *cache)
{
    return zhash_size (cache->zh);
}

static int cache_entry_age (struct cache_entry *hp, int current_epoch)
{
    if (!hp)
        return -1;
    if (hp->lastuse_epoch == 0)
        hp->lastuse_epoch = current_epoch;
    return current_epoch - hp->lastuse_epoch;
}

int cache_expire_entries (struct cache *cache, int current_epoch, int thresh)
{
    zlist_t *keys;
    char *ref;
    struct cache_entry *hp;
    int count = 0;

    if (!(keys = zhash_keys (cache->zh)))
        oom ();
    while ((ref = zlist_pop (keys))) {
        if ((hp = zhash_lookup (cache->zh, ref))
            && !cache_entry_get_dirty (hp)
            && cache_entry_get_valid (hp)
            && (thresh == 0 || cache_entry_age (hp, current_epoch) > thresh)) {
                zhash_delete (cache->zh, ref);
                count++;
        }
        free (ref);
    }
    zlist_destroy (&keys);
    return count;
}

void cache_get_stats (struct cache *cache, tstat_t *ts, int *sizep,
                      int *incompletep, int *dirtyp)
{
    zlist_t *keys;
    struct cache_entry *hp;
    char *ref;
    int size = 0;
    int incomplete = 0;
    int dirty = 0;

    if (!(keys = zhash_keys (cache->zh)))
        oom ();
    while ((ref = zlist_pop (keys))) {
        hp = zhash_lookup (cache->zh, ref);
        if (cache_entry_get_valid (hp)) {
            /* must pass JSON_ENCODE_ANY, object could be anything */
            char *s = json_dumps (hp->o, JSON_ENCODE_ANY);
            if (!s)
                oom ();
            int obj_size = strlen (s);
            free (s);
            size += obj_size;
            tstat_push (ts, obj_size);
        } else
            incomplete++;
        if (cache_entry_get_dirty (hp))
            dirty++;
        free (ref);
    }
    zlist_destroy (&keys);
    if (sizep)
        *sizep = size;
    if (incompletep)
        *incompletep = incomplete;
    if (dirtyp)
        *dirtyp = dirty;
}

int cache_wait_destroy_msg (struct cache *cache, wait_test_msg_f cb, void *arg)
{
    const char *key;
    struct cache_entry *hp;
    int n, count = 0;
    int rc = -1;

    FOREACH_ZHASH (cache->zh, key, hp) {
        if (hp->waitlist_valid) {
            if ((n = wait_destroy_msg (hp->waitlist_valid, cb, arg)) < 0)
                goto done;
            count += n;
        }
        if (hp->waitlist_notdirty) {
            if ((n = wait_destroy_msg (hp->waitlist_notdirty, cb, arg)) < 0)
                goto done;
            count += n;
        }
    }
    rc = count;
done:
    return rc;
}

struct cache *cache_create (void)
{
    struct cache *cache = xzmalloc (sizeof (*cache));
    if (!(cache->zh = zhash_new ()))
        oom ();
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
