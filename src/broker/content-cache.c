/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* See RFC 10 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/log.h"

#include "attr.h"
#include "content-cache.h"

/* A periodic callback purges the cache of least recently used entries.
 * The callback is synchronized wtih the instance heartbeat, within the
 * bounds of 'sync_min' and 'sync_max' seconds.
 */
static double sync_min = 1.;
static double sync_max = 10.;

static const char *default_hash = "sha1";

static const uint32_t default_cache_purge_target_size = 1024*1024*16;
static const uint32_t default_cache_purge_old_entry = 10; // seconds

/* Raise the max blob size value to 1GB so that large KVS values
 * (including KVS directories) can be supported while the KVS transitions
 * to the RFC 11 treeobj data representation.
 */
//static const uint32_t default_blob_size_limit = 1048576; /* RFC 10 */
static const uint32_t default_blob_size_limit = 1048576*1024;

static const uint32_t default_flush_batch_limit = 256;

struct msgstack {
    const flux_msg_t *msg;
    struct msgstack *next;
};

struct cache_entry {
    void *data;
    int len;
    char *blobref;
    uint8_t valid:1;                /* entry contains valid data */
    uint8_t dirty:1;                /* entry needs to be stored upstream */
                                    /*   or to backing store (rank 0) */
    uint8_t load_pending:1;
    uint8_t store_pending:1;
    struct msgstack *load_requests;
    struct msgstack *store_requests;
    double lastused;

    struct cache_entry *lru_prev;
    struct cache_entry *lru_next;
};

struct content_cache {
    flux_t *h;
    flux_reactor_t *reactor;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;
    uint32_t rank;
    zhashx_t *entries;
    uint8_t backing:1;              /* 'content.backing' service available */
    char *backing_name;
    const char *hash_name;
    struct msgstack *flush_requests;

    struct cache_entry *lru_first;
    struct cache_entry *lru_last;

    uint32_t blob_size_limit;
    uint32_t flush_batch_limit;
    uint32_t flush_batch_count;

    uint32_t purge_target_size;
    uint32_t purge_old_entry;

    uint32_t acct_size;             /* total size of all cache entries */
    uint32_t acct_valid;            /* count of valid cache entries */
    uint32_t acct_dirty;            /* count of dirty cache entries */
};

typedef bool (*lru_map_f)(struct content_cache *cache,
                          struct cache_entry *entry,
                          void *arg);

static void flush_respond (struct content_cache *cache);
static int cache_flush (struct content_cache *cache);

static int msgstack_push (struct msgstack **msp, const flux_msg_t *msg)
{
    struct msgstack *ms;
    if (!(ms = malloc (sizeof (*ms))))
        return -1;
    ms->msg = flux_msg_incref (msg);
    ms->next = *msp;
    *msp = ms;
    return 0;
}

static const flux_msg_t *msgstack_pop (struct msgstack **msp)
{
    struct msgstack *ms;
    const flux_msg_t *msg = NULL;

    if ((ms = *msp)) {
        *msp = ms->next;
        msg = ms->msg;
        free (ms);
    }
    return msg;
}

static void msgstack_destroy (struct msgstack **msp)
{
    const flux_msg_t *msg;
    while ((msg = msgstack_pop (msp)))
        flux_msg_decref (msg);
}


/* Respond identically to a list of requests.
 * The list is always run to completion.
 * On error, log at LOG_ERR level.
 */
static void request_list_respond_raw (struct msgstack **l,
                                      flux_t *h,
                                      const void *data,
                                      int len,
                                      const char *type)
{
    const flux_msg_t *msg;
    while ((msg = msgstack_pop (l))) {
        if (flux_respond_raw (h, msg, data, len) < 0)
            flux_log_error (h, "%s (%s):", __FUNCTION__, type);
        flux_msg_decref (msg);
    }
}

/* Same as above only send errnum, errmsg response
 */
static void request_list_respond_error (struct msgstack **l,
                                        flux_t *h,
                                        int errnum,
                                        const char *errmsg,
                                        const char *type)
{
    const flux_msg_t *msg;
    while ((msg = msgstack_pop (l))) {
        if (flux_respond_error (h, msg, errnum, errmsg) < 0)
            flux_log_error (h, "%s (%s):", __FUNCTION__, type);
        flux_msg_decref (msg);
    }
}

/* Add entry to the front of the LRU list.
 */
static void cache_lru_push (struct content_cache *cache,
                            struct cache_entry *entry)
{
    entry->lru_next = cache->lru_first;
    if (cache->lru_last == NULL)
        cache->lru_last = cache->lru_first = entry;
    else {
        cache->lru_first->lru_prev = entry;
        cache->lru_first = entry;
    }
    entry->lastused = flux_reactor_now (cache->reactor);
}

/* Remove entry from LRU list.
 * This is a no-op if entry is not in the LRU list.
 */
static void cache_lru_remove (struct content_cache *cache,
                              struct cache_entry *entry)
{
    if (cache->lru_first == entry)
        cache->lru_first = entry->lru_next;
    else if (entry->lru_prev != NULL)
        entry->lru_prev->lru_next = entry->lru_next;
    if (cache->lru_last == entry)
        cache->lru_last = entry->lru_prev;
    else if (entry->lru_next != NULL)
        entry->lru_next->lru_prev = entry->lru_prev;

    entry->lru_prev = entry->lru_next = NULL;
}

/* Move entry to the front of the LRU list.
 * - if entry is already at the front, this just updates the lastused timestamp
 * - if entry is not in the list, this is equivalent to cache_lru_push()
 */
static void cache_lru_move (struct content_cache *cache,
                            struct cache_entry *entry)
{
    if (cache->lru_first != entry) {
        cache_lru_remove (cache, entry);
        cache_lru_push (cache, entry);
    }
    else
        entry->lastused = flux_reactor_now (cache->reactor);
}

/* Call 'map' function for each cache entry, in reverse-LRU order.
 * - the map function returns true to continue, false to stop iteration
 * - the map function may safely call cache_lru_remove()
 */
static void cache_lru_map (struct content_cache *cache,
                           lru_map_f map,
                           void *arg)
{
    struct cache_entry *e;

    e = cache->lru_last;
    while (e) {
        struct cache_entry *next = e ? e->lru_prev : NULL;
        if (!map (cache, e, arg))
            break;
        e = next;
    }
}

/* Destroy a cache entry
 */
static void cache_entry_destroy (struct cache_entry *e)
{
    if (e) {
        int saved_errno = errno;
        assert (e->load_requests == 0);
        assert (e->store_requests == 0);
        free (e->data);
        msgstack_destroy (&e->load_requests);
        msgstack_destroy (&e->store_requests);
        free (e);
        errno = saved_errno;
    }
}

/* zhashx_destructor_fn footprint
 */
static void cache_entry_destructor (void **item)
{
    if (item) {
        cache_entry_destroy (*item);
        *item = NULL;
    }
}

/* Create a cache entry.
 * Entries are created with a blobref, but no data (e.g. "invalid").
 * The blobref is copied to the space following the entry struct so the entry
 * and the blobref can be co-located in memory, and allocated with one malloc.
 * Returns entry on success, NULL with errno set on failure.
 */
static struct cache_entry *cache_entry_create (const char *blobref)
{
    struct cache_entry *e;
    int bloblen = strlen (blobref) + 1;

    if (!(e = calloc (1, sizeof (*e) + bloblen)))
        return NULL;
    e->blobref = (char *)(e + 1);
    memcpy (e->blobref, blobref, bloblen);
    return e;
}

/* Make an invalid cache entry valid, filling in its data.
 * Set dirty flag if 'dirty' is true.
 * Perform accounting.
 * Respond to any pending load requests.
 * If entry is already valid, do not fill, do not set dirty; there will be
 * no load requests.
 * Returns 0 on success, -1 on failure with errno set.
 */
static int cache_entry_fill (struct content_cache *cache,
                             struct cache_entry *e,
                             const void *data,
                             int len,
                             bool dirty)
{
    if (!e->valid) {
        assert (!e->data);
        assert (e->len == 0);
        if (len > 0) {
            if (!(e->data = malloc (len)))
                return -1;
            memcpy (e->data, data, len);
        }
        e->len = len;
        e->valid = 1;
        cache->acct_valid++;
        cache->acct_size += len;
        if (dirty) {
            e->dirty = 1;
            cache->acct_dirty++;
        }
        request_list_respond_raw (&e->load_requests,
                                  cache->h,
                                  e->data,
                                  e->len,
                                  "load");

        /* Push entry onto front of LRU upon transition from invalid to valid,
         * but only if entry is not dirty.
         */
        if (!e->dirty)
            cache_lru_push (cache, e);
    }
    return 0;
}

static void cache_entry_dirty_clear (struct content_cache *cache,
                                     struct cache_entry *e)
{
    if (e->dirty) {
        cache->acct_dirty--;
        e->dirty = 0;
        request_list_respond_raw (&e->store_requests,
                                  cache->h,
                                  e->blobref,
                                  strlen (e->blobref) + 1,
                                  "store");

        /* Push entry onto front of LRU upon transition from dirty to clean.
         */
        cache_lru_push (cache, e);
    }
}


/* Create and insert a cache entry, using 'blobref' as the hash key.
 * Returns 0 on success, -1 on failure with errno set.
 */
static struct cache_entry *cache_entry_insert (struct content_cache *cache,
                                               const char *blobref)
{
    struct cache_entry *e;
    if (!(e = cache_entry_create (blobref)))
        return NULL;
    if (zhashx_insert (cache->entries, e->blobref, e) < 0) {
        errno = EEXIST;
        cache_entry_destroy (e);
        return NULL;
    }
    return e;
}

/* Look up a cache entry, by blobref.
 * Returns entry on success, NULL on failure.
 * N.B. errno is not set
 */
static struct cache_entry *cache_entry_lookup (struct content_cache *cache,
                                               const char *blobref)
{
    struct cache_entry *e;
    if (!(e = zhashx_lookup (cache->entries, blobref)))
        return NULL;

    /* Move to front of LRU if valid and not dirty.
     */
    if (e->valid && !e->dirty)
        cache_lru_move (cache, e);
    return e;
}

/* Remove a cache entry.
 */
static void cache_entry_remove (struct content_cache *cache,
                                struct cache_entry *e)
{
    assert (e->load_requests == NULL);
    assert (e->store_requests == NULL);
    cache_lru_remove (cache, e);
    if (e->valid) {
        cache->acct_size -= e->len;
        cache->acct_valid--;
    }
    if (e->dirty)
        cache->acct_dirty--;
    zhashx_delete (cache->entries, e->blobref);
}

/* Load operation
 *
 * If a cache entry is already present and valid, response is immediate.
 * Otherwise request is queued on the invalid cache entry, and a new
 * request is sent to the next level of TBON, or on rank 0, to the
 * content.backing service.  At most a single request is sent per cache entry.
 * Once the response is received, identical responses are sent to all
 * parked requests, and cache entry is made valid or removed if there was
 * an error such as ENOENT.
 */

static void cache_load_continuation (flux_future_t *f, void *arg)
{
    struct content_cache *cache = arg;
    struct cache_entry *e = flux_future_aux_get (f, "entry");
    const void *data = NULL;
    int len = 0;

    e->load_pending = 0;
    if (flux_content_load_get (f, &data, &len) < 0) {
        if (errno == ENOSYS && cache->rank == 0)
            errno = ENOENT;
        if (errno != ENOENT)
            flux_log_error (cache->h, "content load");
        goto error;
    }
    if (cache_entry_fill (cache, e, data, len, false) < 0) {
        flux_log_error (cache->h, "content load");
        goto error;
    }
    flux_future_destroy (f);
    return;
error:
    request_list_respond_error (&e->load_requests,
                                cache->h,
                                errno,
                                NULL,
                                "load");
    cache_entry_remove (cache, e);
    flux_future_destroy (f);
}

static int cache_load (struct content_cache *cache, struct cache_entry *e)
{
    flux_future_t *f;
    int flags = CONTENT_FLAG_UPSTREAM;

    if (e->load_pending)
        return 0;
    if (cache->rank == 0)
        flags = CONTENT_FLAG_CACHE_BYPASS;
    if (!(f = flux_content_load (cache->h, e->blobref, flags))
        || flux_future_aux_set (f, "entry", e, NULL) < 0
        || flux_future_then (f, -1., cache_load_continuation, cache) < 0) {
        if (errno == ENOSYS && cache->rank == 0)
            errno = ENOENT;
        if (errno != ENOENT)
            flux_log_error (cache->h, "content load");
        flux_future_destroy (f);
        return -1;
    }
    e->load_pending = 1;
    return 0;
}

void content_load_request (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct content_cache *cache = arg;
    const char *blobref;
    int blobref_size;
    void *data = NULL;
    int len = 0;
    struct cache_entry *e;

    if (flux_request_decode_raw (msg, NULL, (const void **)&blobref,
                                 &blobref_size) < 0)
        goto error;
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        goto error;
    }
    if (!(e = cache_entry_lookup (cache, blobref))) {
        if (cache->rank == 0 && !cache->backing) {
            errno = ENOENT;
            goto error;
        }
        if (!(e = cache_entry_insert (cache, blobref))) {
            flux_log_error (h, "content load");
            goto error;
        }
    }
    if (!e->valid) {
        if (cache_load (cache, e) < 0)
            goto error;
        if (msgstack_push (&e->load_requests, msg) < 0) {
            flux_log_error (h, "content load");
            goto error;
        }
        return; /* RPC continuation will respond to msg */
    }
    data = e->data;
    len = e->len;
    if (flux_respond_raw (h, msg, data, len) < 0)
        flux_log_error (h, "content load: flux_respond_raw");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "content load: flux_respond_error");
}

/* Store operation
 *
 * If a cache entry is already valid and not dirty, response is immediate.
 * If cache entry is invalid, it is made valid (responding to any queued
 * load requests), and then dirty.
 *
 * Dirty cache is write-through for ranks > 0;  that is, a request is queued
 * and a single store request per cache entry is sent to the next level
 * of TBON.  Once present in the rank 0 cache, requests are unwound and
 * responded to at each level.
 *
 * Dirty cache is write-back for rank 0, that is;  the response is immediate
 * even though the entry may be dirty with respect to a 'content.backing'
 * service.  This allows the cache to be updated at memory speeds,
 * while holding the invariant that after a store RPC returns, the entry may
 * be loaded from any rank.  The optional content.backing service can
 * offload rank 0 hash entries at a slower pace.
 */

/* If cache has been flushed, respond to flush requests, if any.
 * If there are still dirty entries and the number of outstanding
 * store requests would not exceed the limit, flush more entries.
 * Optimization: since scanning for dirty entries is a linear search,
 * only do it when the number of outstanding store requests falls to
 * a low water mark, here hardwired to be half of the limit.
 */
static void cache_resume_flush (struct content_cache *cache)
{
    if (cache->acct_dirty == 0 || (cache->rank == 0 && !cache->backing))
        flush_respond (cache);
    else if (cache->acct_dirty - cache->flush_batch_count > 0
            && cache->flush_batch_count <= cache->flush_batch_limit / 2)
        (void)cache_flush (cache); /* resume flushing */
}

static void cache_store_continuation (flux_future_t *f, void *arg)
{
    struct content_cache *cache = arg;
    struct cache_entry *e = flux_future_aux_get (f, "entry");
    const char *blobref;

    e->store_pending = 0;
    assert (cache->flush_batch_count > 0);
    cache->flush_batch_count--;
    if (flux_content_store_get (f, &blobref) < 0) {
        if (cache->rank == 0 && errno == ENOSYS)
            flux_log (cache->h, LOG_DEBUG, "content store: %s",
                      "backing store service unavailable");
        else
            flux_log_error (cache->h, "content store");
        goto error;
    }
    if (strcmp (blobref, e->blobref)) {
        flux_log (cache->h, LOG_ERR, "content store: wrong blobref");
        errno = EIO;
        goto error;
    }
    cache_entry_dirty_clear (cache, e);
    flux_future_destroy (f);
    cache_resume_flush (cache);
    return;
error:
    request_list_respond_error (&e->store_requests,
                                cache->h,
                                errno,
                                NULL,
                                "store");
    flux_future_destroy (f);
    cache_resume_flush (cache);
}

static int cache_store (struct content_cache *cache, struct cache_entry *e)
{
    flux_future_t *f;
    int flags = CONTENT_FLAG_UPSTREAM;

    assert (e->valid);

    if (e->store_pending)
        return 0;
    if (cache->rank == 0) {
        if (cache->flush_batch_count >= cache->flush_batch_limit)
            return 0;
        flags = CONTENT_FLAG_CACHE_BYPASS;
    }
    if (!(f = flux_content_store (cache->h, e->data, e->len, flags))
        || flux_future_aux_set (f, "entry", e, NULL) < 0
        || flux_future_then (f, -1., cache_store_continuation, cache) < 0) {
        flux_log_error (cache->h, "content store");
        flux_future_destroy (f);
        return -1;
    }
    e->store_pending = 1;
    cache->flush_batch_count++;
    return 0;
}

static void content_store_request (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct content_cache *cache = arg;
    const void *data;
    int len;
    struct cache_entry *e = NULL;
    char blobref[BLOBREF_MAX_STRING_SIZE];

    if (flux_request_decode_raw (msg, NULL, &data, &len) < 0)
        goto error;
    if (len > cache->blob_size_limit) {
        errno = EFBIG;
        goto error;
    }
    if (blobref_hash (cache->hash_name, (uint8_t *)data, len, blobref,
                      sizeof (blobref)) < 0)
        goto error;

    if (!(e = cache_entry_lookup (cache, blobref))) {
        if (!(e = cache_entry_insert (cache, blobref)))
            goto error;
    }
    if (cache_entry_fill (cache, e, data, len, true) < 0)
        goto error;
    if (e->dirty) {
        if (cache->rank > 0 || cache->backing) {
            if (cache_store (cache, e) < 0)
                goto error;
            if (cache->rank > 0) {  /* write-through */
                if (msgstack_push (&e->store_requests, msg) < 0)
                    goto error;
                return;
            }
        }
    }
    if (flux_respond_raw (h, msg, blobref, strlen (blobref) + 1) < 0)
        flux_log_error (h, "content store: flux_respond_raw");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "content store: flux_respond_error");
}

/* Backing store is enabled/disabled by modules that provide the
 * 'content.backing' service.  At module load time, the backing module
 * informs the content service of its availability, and entries are
 * asynchronously duplicated on the backing store and made eligible for
 * dropping from the rank 0 cache.
 */

static int cache_flush (struct content_cache *cache)
{
    struct cache_entry *e;
    const char *key;
    int saved_errno = 0;
    int count = 0;
    int rc = 0;

    if (cache->acct_dirty - cache->flush_batch_count == 0
            || cache->flush_batch_count >= cache->flush_batch_limit)
        return 0;

    flux_log (cache->h, LOG_DEBUG, "content flush begin");
    FOREACH_ZHASHX (cache->entries, key, e) {
        if (!e->dirty || e->store_pending)
            continue;
        if (cache_store (cache, e) < 0) {
            saved_errno = errno;
            rc = -1;
        }
        count++;
        if (cache->flush_batch_count >= cache->flush_batch_limit)
            break;
    }
    flux_log (cache->h, LOG_DEBUG, "content flush +%d (dirty=%d pending=%d)",
              count, cache->acct_dirty, cache->flush_batch_count);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void content_register_backing_request (flux_t *h,
                                              flux_msg_handler_t *mh,
                                              const flux_msg_t *msg,
                                              void *arg)
{
    struct content_cache *cache = arg;
    const char *name;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (cache->rank != 0) {
        errno = EINVAL;
        errstr = "content backing store can only be registered on rank 0";
        goto error;
    }
    if (cache->backing) {
        errno = EBUSY;
        errstr = "content backing store is already active";
        goto error;
    }
    /* cache->backing_name is either set to the initial value of the
     * "content.backing-module" attribute (e.g. from broker command line),
     * or to the first-registered backing store name.  Once set, it cannot
     * be changed.
     */
    if (!cache->backing_name) {
        if (!(cache->backing_name = strdup (name)))
            goto error;
    }
    if (strcmp (cache->backing_name, name) != 0) {
        errno = EINVAL;
        errstr = "content backing store cannot be changed on the fly";
        goto error;
    }
    cache->backing = 1;
    flux_log (h, LOG_DEBUG, "content backing store: enabled %s", name);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to register-backing request");
    (void)cache_flush (cache);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to register-backing request");
};

static void content_unregister_backing_request (flux_t *h,
                                                flux_msg_handler_t *mh,
                                                const flux_msg_t *msg,
                                                void *arg)
{
    struct content_cache *cache = arg;
    const char *errstr = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!cache->backing) {
        errno = EINVAL;
        errstr = "content backing store is not active";
        goto error;
    }
    cache->backing = 0;
    flux_log (h, LOG_DEBUG, "content backing store: disabled");
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to unregister-backing request");
    if (cache->acct_dirty > 0)
        flux_log (h, LOG_ERR, "%d unflushables", cache->acct_dirty);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to unregister-backing request");
}

/* Forcibly drop all entries from the cache that can be dropped
 * without data loss.
 */

static bool cache_drop_map (struct content_cache *cache,
                            struct cache_entry *e,
                            void *arg)
{
    cache_entry_remove (cache, e);
    return true;
}

static void content_dropcache_request (flux_t *h, flux_msg_handler_t *mh,
                                       const flux_msg_t *msg, void *arg)
{
    struct content_cache *cache = arg;
    int orig_size;

    orig_size = zhashx_size (cache->entries);
    cache_lru_map (cache, cache_drop_map, NULL);
    flux_log (h, LOG_DEBUG, "content dropcache %d/%d",
              orig_size - (int)zhashx_size (cache->entries), orig_size);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "content dropcache");
}

/* Return stats about the cache.
 */

static void content_stats_request (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct content_cache *cache = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i s:i s:i s:i s:i}",
                           "count", zhashx_size (cache->entries),
                           "valid", cache->acct_valid,
                           "dirty", cache->acct_dirty,
                           "size", cache->acct_size,
                           "flush-batch-count", cache->flush_batch_count) < 0)
        flux_log_error (h, "content stats");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "content stats");
}

/* Flush all dirty entries by walking the entire cache, issuing store
 * requests for all dirty entries.  Responses are handled asynchronously
 * using RPC continuations.  A response to the flush request is not sent
 * until all the store responses are received.  If 'backing' is false on
 * rank 0, we go ahead and issue the store requests and handle the ENOSYS
 * errors that result.
 */

/* This is called when outstanding store ops have completed.  */
static void flush_respond (struct content_cache *cache)
{
    if (!cache->acct_dirty) {
        request_list_respond_raw (&cache->flush_requests,
                                  cache->h,
                                  NULL,
                                  0,
                                  "flush");
    }
    else {
        errno = EIO;
        if (cache->rank == 0 && !cache->backing)
            errno = ENOSYS;
        request_list_respond_error (&cache->flush_requests,
                                    cache->h,
                                    errno,
                                    NULL,
                                    "flush");
    }
}

static void content_flush_request (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct content_cache *cache = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (cache->acct_dirty != 0) {
        if (cache_flush (cache) < 0)
            goto error;
        if (cache->acct_dirty > 0) {
            if (msgstack_push (&cache->flush_requests, msg) < 0)
                goto error;
            return;
        }
        if (cache->acct_dirty > 0) {
            errno = EIO;
            goto error;
        }
    }
    flux_log (h, LOG_DEBUG, "content flush");
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "content flush");
    return;
error:
    flux_log (h, LOG_DEBUG, "content flush: %s", flux_strerror (errno));
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "content flush");
}

/* Heartbeat drives periodic cache purge
 */

static bool cache_purge_map (struct content_cache *cache,
                             struct cache_entry *e,
                             void *arg)
{
    double *now = arg;

    if (cache->acct_size <= cache->purge_target_size
        || *now - e->lastused < cache->purge_old_entry)
        return false;
    /* Entries are not placed in the LRU until they are valid and not dirty.
     */
    assert (e->valid);
    assert (!e->dirty);
    cache_entry_remove (cache, e);
    return true;
}

static void cache_purge (struct content_cache *cache)
{
    double now = flux_reactor_now (cache->reactor);

    cache_lru_map (cache, cache_purge_map, &now);
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct content_cache *cache = arg;

    cache_purge (cache);

    flux_future_reset (f);
}

/* Initialization
 */

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content.load",
        content_load_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.store",
        content_store_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.unregister-backing",
        content_unregister_backing_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.register-backing",
        content_register_backing_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.dropcache",
        content_dropcache_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.stats.get",
        content_stats_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.flush",
        content_flush_request,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static int content_cache_getattr (const char *name, const char **val, void *arg)
{
    struct content_cache *cache = arg;

    if (!strcmp (name, "content.backing-module"))
        *val = cache->backing_name;
    else
        return -1;
    return 0;
}

static int register_attrs (struct content_cache *cache, attr_t *attr)
{
    const char *s;

    /* Take initial value of content->backing_name from content.backing-module
     * attribute, if set.  The attribute is re-added below.
     */
    if (attr_get (attr, "content.backing-module", &s, NULL) == 0) {
        if (!(cache->backing_name = strdup (s)))
            return -1;
        if (attr_delete (attr, "content.backing-module", 1) < 0)
            return -1;
    }

    /* content.hash may be set on the command line.
     */
    if (attr_get (attr, "content.hash", &s, NULL) < 0) {
        if (attr_add (attr,
                      "content.hash",
                      cache->hash_name,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
            return -1;
    }
    else {
        if (blobref_validate_hashtype (s) < 0) {
            log_msg ("%s: unknown hash type", s);
            return -1;
        }
        if (attr_set_flags (attr, "content.hash", FLUX_ATTRFLAG_IMMUTABLE) < 0)
            return -1;
        cache->hash_name = s;
    }

    /* Purge tunables
     */
    if (attr_add_active_uint32 (attr, "content.purge-target-size",
                &cache->purge_target_size, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content.purge-old-entry",
                &cache->purge_old_entry, 0) < 0)
        return -1;
    /* Misc
     */
    if (attr_add_active_uint32 (attr, "content.flush-batch-limit",
                &cache->flush_batch_limit, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content.blob-size-limit",
                &cache->blob_size_limit, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_active (attr, "content.backing-module",FLUX_ATTRFLAG_READONLY,
                 content_cache_getattr, NULL, cache) < 0)
        return -1;

    return 0;
}

void content_cache_destroy (struct content_cache *cache)
{
    if (cache) {
        int saved_errno = errno;
        flux_future_destroy (cache->f_sync);
        flux_msg_handler_delvec (cache->handlers);
        free (cache->backing_name);
        zhashx_destroy (&cache->entries);
        msgstack_destroy (&cache->flush_requests);
        free (cache);
        errno = saved_errno;
    }
}

struct content_cache *content_cache_create (flux_t *h, attr_t *attrs)
{
    struct content_cache *cache;

    if (!(cache = calloc (1, sizeof (*cache))))
        return NULL;
    if (!(cache->entries = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (cache->entries, cache_entry_destructor);
    zhashx_set_key_destructor (cache->entries, NULL); // key is part of entry
    zhashx_set_key_duplicator (cache->entries, NULL); // key is part of entry

    cache->rank = FLUX_NODEID_ANY;
    cache->blob_size_limit = default_blob_size_limit;
    cache->flush_batch_limit = default_flush_batch_limit;
    cache->purge_target_size = default_cache_purge_target_size;
    cache->purge_old_entry = default_cache_purge_old_entry;
    cache->hash_name = default_hash;
    cache->h = h;
    cache->reactor = flux_get_reactor (h);

    if (register_attrs (cache, attrs) < 0)
        goto error;

    if (flux_msg_handler_addvec (h, htab, cache, &cache->handlers) < 0)
        goto error;
    if (flux_get_rank (h, &cache->rank) < 0)
        goto error;
    if (!(cache->f_sync = flux_sync_create (h, sync_min))
        || flux_future_then (cache->f_sync, sync_max, sync_cb, cache) < 0)
        goto error;
    return cache;
nomem:
    errno = ENOMEM;
error:
    content_cache_destroy (cache);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
