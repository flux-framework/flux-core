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
#include "src/common/libccan/ccan/list/list.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/log.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "cache.h"
#include "checkpoint.h"
#include "mmap.h"

/* A periodic callback purges the cache of least recently used entries.
 * The callback is synchronized with the instance heartbeat, with a
 * sync period upper bound set to 'sync_max' seconds.
 */
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

/* Hash digests are used as zhashx keys.  The digest size needs to be
 * available to zhashx comparator so make this global.
 */
static int content_hash_size;

struct msgstack {
    const flux_msg_t *msg;
    struct msgstack *next;
};

struct cache_entry {
    const void *data;
    size_t len;
    void *data_container;
    void *hash;                     // key storage is contiguous with struct
    uint8_t valid:1;                // entry contains valid data
    uint8_t dirty:1;                // entry needs to be stored upstream
                                    //   or to backing store (rank 0)
    uint8_t ephemeral:1;            // clean entry is not on backing store
    uint8_t load_pending:1;
    uint8_t store_pending:1;
    uint8_t mmapped:1;
    struct msgstack *load_requests;
    struct msgstack *store_requests;
    double lastused;

    struct list_node list;
};

struct content_cache {
    flux_t *h;
    flux_reactor_t *reactor;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;
    uint32_t rank;
    zhashx_t *entries;
    uint8_t backing:1;              // 'content.backing' service available
    char *backing_name;
    char *hash_name;
    struct msgstack *flush_requests;

    struct list_head lru;           // LRU is for valid, clean entries only
    struct list_head flush;         // dirties queued due to batch limit

    uint32_t blob_size_limit;
    uint32_t flush_batch_limit;
    uint32_t flush_batch_count;
    int flush_errno;

    uint32_t purge_target_size;
    uint32_t purge_old_entry;

    uint64_t acct_size;             // total size of all cache entries
    uint32_t acct_valid;            // count of valid cache entries
    uint32_t acct_dirty;            // count of dirty cache entries

    struct content_checkpoint *checkpoint;
    struct content_mmap *mmap;
};

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
                                      int flag,
                                      const void *data,
                                      int len,
                                      const char *type)
{
    const flux_msg_t *msg;
    while ((msg = msgstack_pop (l))) {
        flux_msg_t *response;
        if (!(response = flux_response_derive (msg, 0))
            || flux_msg_set_payload (response, data, len) < 0
            || flux_msg_set_flag (response, flag) < 0
            || flux_send (h, response, 0) < 0)
            flux_log_error (h, "%s (%s):", __FUNCTION__, type);
        flux_msg_decref (response);
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

/* Destroy a cache entry
 */
static void cache_entry_destroy (struct cache_entry *e)
{
    if (e) {
        int saved_errno = errno;
        msgstack_destroy (&e->load_requests);
        msgstack_destroy (&e->store_requests);
        if (e->mmapped)
            content_mmap_region_decref (e->data_container);
        else
            flux_msg_decref (e->data_container);
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
/* zhashx_hash_fn footprint
 */
static size_t cache_entry_hasher (const void *key)
{
    return *(size_t *)key;
}
/* zhashx_comparator_fn footprint
 */
static int cache_entry_comparator (const void *item1, const void *item2)
{
    return memcmp (item1, item2, content_hash_size);
}

/* Create a cache entry.
 * Entries are created with no data (e.g. "invalid").
 * Returns entry on success, NULL with errno set on failure.
 */
static struct cache_entry *cache_entry_create (const void *hash)
{
    struct cache_entry *e;

    if (!(e = calloc (1, sizeof (*e) + content_hash_size)))
        return NULL;
    e->hash = (char *)(e + 1);
    memcpy (e->hash, hash, content_hash_size);
    list_node_init (&e->list);
    return e;
}

static void cache_entry_dirty_clear (struct content_cache *cache,
                                     struct cache_entry *e)
{
    if (e->dirty) {
        cache->acct_dirty--;
        e->dirty = 0;

        assert (e->valid);
        list_add (&cache->lru, &e->list);
        e->lastused = flux_reactor_now (cache->reactor);

        request_list_respond_raw (&e->store_requests,
                                  cache->h,
                                  0,
                                  e->hash,
                                  content_hash_size,
                                  "store");
    }
}


/* Create and insert a cache entry.
 * Returns 0 on success, -1 on failure with errno set.
 */
static struct cache_entry *cache_entry_insert (struct content_cache *cache,
                                               const void *hash,
                                               int hash_size)
{
    struct cache_entry *e;

    if (hash_size != content_hash_size) {
        errno = EINVAL;
        return NULL;
    }
    if (!(e = cache_entry_create (hash)))
        return NULL;
    if (zhashx_insert (cache->entries, e->hash, e) < 0) {
        errno = EEXIST;
        cache_entry_destroy (e);
        return NULL;
    }
    return e;
}

/* Look up a cache entry.
 * Move to front of LRU because it was looked up.
 * Returns entry on success, NULL on failure.
 * N.B. errno is not set
 */
static struct cache_entry *cache_entry_lookup (struct content_cache *cache,
                                               const void *hash,
                                               int hash_size)
{
    struct cache_entry *e;

    if (hash_size != content_hash_size)
        return NULL;
    if (!(e = zhashx_lookup (cache->entries, hash)))
        return NULL;

    if (e->valid && !e->dirty) {
        list_del_from (&cache->lru, &e->list);
        list_add (&cache->lru, &e->list);
        e->lastused = flux_reactor_now (cache->reactor);
    }

    return e;
}

/* Remove a cache entry.
 */
static void cache_entry_remove (struct content_cache *cache,
                                struct cache_entry *e)
{
    assert (e->load_requests == NULL);
    assert (e->store_requests == NULL);
    assert (!e->dirty);
    list_del (&e->list);
    if (e->valid) {
        cache->acct_size -= e->len;
        cache->acct_valid--;
    }
    zhashx_delete (cache->entries, e->hash);
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
    const flux_msg_t *msg;
    const char *errmsg = NULL;

    e->load_pending = 0;
    if (flux_future_get (f, (const void **)&msg) < 0) {
        if (errno == ENOSYS && cache->rank == 0)
            errno = ENOENT;
        if (errno != ENOENT)
            flux_log_error (cache->h, "content load");
        errmsg = flux_future_error_string (f);
        goto error;
    }
    /* N.B. the entry may already be valid if a store filled it while
     * we were waiting for this load completion.  Do nothing in that case.
     * Any pending load requests would have been answered already.
     */
    if (!e->valid) {
        assert (!e->data_container);
        assert (!e->dirty);
        if (flux_response_decode_raw (msg, NULL, &e->data, &e->len) < 0) {
            flux_log_error (cache->h, "content load");
            goto error;
        }
        e->data_container = (void *)flux_msg_incref (msg);
        e->valid = 1;
        if (flux_msg_has_flag (msg, FLUX_MSGFLAG_USER1))
            e->ephemeral = 1;
        cache->acct_valid++;
        cache->acct_size += e->len;
        list_add (&cache->lru, &e->list);
        e->lastused = flux_reactor_now (cache->reactor);
        request_list_respond_raw (&e->load_requests,
                                  cache->h,
                                  e->ephemeral ? FLUX_MSGFLAG_USER1 : 0,
                                  e->data,
                                  e->len,
                                  "load");
    }
    flux_future_destroy (f);
    return;
error:
    request_list_respond_error (&e->load_requests,
                                cache->h,
                                errno,
                                errmsg,
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
    if (!(f = content_load_byhash (cache->h, e->hash, content_hash_size, flags))
        || flux_future_aux_set (f, "entry", e, NULL) < 0
        || flux_future_then (f, -1., cache_load_continuation, cache) < 0) {
        flux_log_error (cache->h, "content load");
        flux_future_destroy (f);
        return -1;
    }
    e->load_pending = 1;
    return 0;
}

static void content_load_request (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct content_cache *cache = arg;
    const void *hash;
    size_t hash_size;
    struct cache_entry *e;
    const char *errmsg = NULL;

    if (flux_request_decode_raw (msg, NULL, &hash, &hash_size) < 0)
        goto error;
    if (hash_size != content_hash_size) {
        errno = EPROTO;
        goto error;
    }
    if (!(e = cache_entry_lookup (cache, hash, hash_size))) {
        struct content_region *region = NULL;
        const void *data = NULL;
        int len = 0;

        if (cache->rank == 0) {
            region = content_mmap_region_lookup (cache->mmap,
                                                 hash,
                                                 hash_size,
                                                 &data,
                                                 &len);
            if (!region && !cache->backing) {
                errno = ENOENT;
                goto error;
            }
        }
        if (!(e = cache_entry_insert (cache, hash, hash_size))) {
            flux_log_error (h, "content load");
            goto error;
        }
        if (region) {
            e->data_container = content_mmap_region_incref (region);
            e->data = data;
            e->len = len;
            e->valid = 1;
            e->ephemeral = 1;
            e->mmapped = 1;
            cache->acct_valid++;
            cache->acct_size += e->len;
            list_add (&cache->lru, &e->list);
            e->lastused = flux_reactor_now (cache->reactor);
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
    if (e->valid && e->mmapped) { // rank 0 only
        if (!content_mmap_validate (e->data_container,
                                    e->hash,
                                    content_hash_size,
                                    e->data,
                                    e->len)) {
            errmsg = "mapped file content has changed";
            errno = EINVAL;
            goto error;
        }
    }

    /* Send load response with FLUX_MSGFLAG_USER1 representing the
     * ephemeral flag, if set.
     */
    flux_msg_t *response;
    if (!(response = flux_response_derive (msg, 0))
        || flux_msg_set_payload (response, e->data, e->len) < 0
        || (e->ephemeral
            && flux_msg_set_flag (response, FLUX_MSGFLAG_USER1) < 0)
        || flux_send (h, response, 0) < 0) {
        flux_log_error (h, "content load: error sending response");
    }
    flux_msg_decref (response);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
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
 * Dirty cache is write-back for rank 0; that is, the response is immediate
 * even though the entry may be dirty with respect to a 'content.backing'
 * service.  This allows the cache to be updated at memory speeds,
 * while holding the invariant that after a store RPC returns, the entry may
 * be loaded from any rank.  The optional content.backing service can
 * offload rank 0 hash entries at a slower pace.
 */

/* If cache has been flushed, respond to flush requests, if any.
 * If there are still dirty entries in the cache->flush queue waiting to
 * be stored, call cache_flush() to see if we can start any more.
 */
static void cache_resume_flush (struct content_cache *cache)
{
    if (cache->acct_dirty == 0 || (cache->rank == 0 && !cache->backing))
        flush_respond (cache);
    else
        (void)cache_flush (cache); /* resume flushing, subject to limits */
}

static void cache_store_continuation (flux_future_t *f, void *arg)
{
    struct content_cache *cache = arg;
    struct cache_entry *e = flux_future_aux_get (f, "entry");
    const void *hash;
    size_t hash_size;

    e->store_pending = 0;
    assert (cache->flush_batch_count > 0);
    cache->flush_batch_count--;
    if (content_store_get_hash (f, &hash, &hash_size) < 0) {
        if (cache->rank == 0 && errno == ENOSYS) {
            flux_log (cache->h,
                      LOG_DEBUG,
                      "content store: %s",
                      "backing store service unavailable");
        }
        else {
            flux_log (cache->h,
                      LOG_CRIT,
                      "content store: %s",
                      strerror (errno));
        }
        goto error;
    }
    if (hash_size != content_hash_size
        || memcmp (hash, e->hash, content_hash_size) != 0) {
        errno = EIO;
        goto error;
    }
    cache_entry_dirty_clear (cache, e);
    /* clear flush errno if backing store functional/recovered */
    cache->flush_errno = 0;
    flux_future_destroy (f);
    cache_resume_flush (cache);
    return;
error:
    request_list_respond_error (&e->store_requests,
                                cache->h,
                                errno,
                                NULL,
                                "store");
    /* all flush requests are assumed to fail with same errno */
    request_list_respond_error (&cache->flush_requests,
                                cache->h,
                                errno,
                                NULL,
                                "flush");
    cache->flush_errno = errno;
    flux_future_destroy (f);
    cache_resume_flush (cache);
}

/* Issue #4482, there is a small chance a dirty entry could be added
 * to the flush list twice which can lead to list corruption.  As an
 * extra measure, perform a delete from the list first.  If the node
 * is not on the list, the delete is a no-op.
 */
static void flush_list_append (struct content_cache *cache,
                               struct cache_entry *e)
{
    list_del (&e->list);
    list_add_tail (&cache->flush, &e->list);
}

static int cache_store (struct content_cache *cache, struct cache_entry *e)
{
    flux_future_t *f;
    int flags = CONTENT_FLAG_UPSTREAM;

    assert (e->valid);

    if (e->store_pending)
        return 0;
    if (cache->rank == 0) {
        if (cache->flush_batch_count >= cache->flush_batch_limit) {
            flush_list_append (cache, e);
            return 0;
        }
        flags = CONTENT_FLAG_CACHE_BYPASS;
    }
    if (!(f = content_store (cache->h, e->data, e->len, flags))
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

static void content_store_request (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct content_cache *cache = arg;
    const void *data;
    size_t len;
    struct cache_entry *e = NULL;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_size;

    if (flux_request_decode_raw (msg, NULL, &data, &len) < 0)
        goto error;
    if (len > cache->blob_size_limit) {
        errno = EFBIG;
        goto error;
    }
    if ((hash_size = blobref_hash_raw (cache->hash_name,
                                       data,
                                       len,
                                       hash,
                                       sizeof (hash))) < 0)
        goto error;
    /* If existing entry has the ephemeral bit set, remove it and let it be
     * replaced with a new entry.  N.B. it can be assumed that an entry with
     * the ephemeral bit set is valid and not dirty.
     */
    if ((e = cache_entry_lookup (cache, hash, hash_size))
        && e->ephemeral) {
        cache_entry_remove (cache, e);
        e = NULL;
    }
    if (!e) {
        if (!(e = cache_entry_insert (cache, hash, hash_size)))
            goto error;
    }
    /* Fill invalid cache entry, which may have been just created above,
     * or could be there because a load was requested and it still awaits
     * a response from up.  For the latter case, respond to any pending load
     * requests after we fill the entry.
     */
    if (!e->valid) {
        assert (!e->data_container);
        e->data = data;
        e->len = len;
        e->data_container = (void *)flux_msg_incref (msg);
        e->valid = 1;
        e->dirty = 1;
        cache->acct_valid++;
        cache->acct_size += e->len;
        cache->acct_dirty++;
        request_list_respond_raw (&e->load_requests,
                                  cache->h,
                                  0,
                                  e->data,
                                  e->len,
                                  "load");
    }
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
        /* On rank 0, save to flush list in event backing module
         * loaded later.  Note that dirty entries are not removed
         * during purge or dropcache, so this does not alter
         * behavior */
        if (cache->rank == 0 && !cache->backing)
            flush_list_append (cache, e);
    }
    if (flux_respond_raw (h, msg, hash, hash_size) < 0)
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
    int last_errno = 0;
    int rc = 0;

    while (cache->flush_batch_count < cache->flush_batch_limit) {
        if (!(e = list_top (&cache->flush, struct cache_entry, list)))
            break;
        if (cache_store (cache, e) < 0) { // incr flush_batch_count
            last_errno = errno;           //   and continuation will decr
            rc = -1;
            /* A few errors we will consider "unrecoverable", so break
             * out */
            if (errno == ENOSYS
                || errno == ENOMEM)
                break;
        }
        (void)list_pop (&cache->flush, struct cache_entry, list);
    }
    if (rc < 0)
        errno = last_errno;
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
        if (!(cache->backing_name = strdup (name))
            || flux_attr_set (h,
                              "content.backing-module",
                              cache->backing_name) < 0)
            goto error;
    }
    if (!streq (cache->backing_name, name)) {
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
    /* If backing store is unloaded with pending flush requests, ensure that
     * they receive an error response.
     */
    if (cache->flush_requests) {
        request_list_respond_error (&cache->flush_requests,
                                    cache->h,
                                    ENOSYS,
                                    NULL,
                                    "flush");
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to unregister-backing request");
}

/* Forcibly drop all entries from the cache that can be dropped
 * without data loss.  Use the LRU for this since all entries are
 * valid and clean.
 */

static void content_dropcache_request (flux_t *h,
                                       flux_msg_handler_t *mh,
                                       const flux_msg_t *msg,
                                       void *arg)
{
    struct content_cache *cache = arg;
    int orig_size;
    struct cache_entry *e = NULL;
    struct cache_entry *next;

    orig_size = zhashx_size (cache->entries);

    list_for_each_safe (&cache->lru, e, next, list) {
        cache_entry_remove (cache, e);
    }

    flux_log (h, LOG_DEBUG, "content dropcache %d/%d",
              orig_size - (int)zhashx_size (cache->entries), orig_size);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "content dropcache");
}

/* Return stats about the cache.
 */

static void content_stats_request (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct content_cache *cache = arg;
    json_t *o = content_mmap_get_stats (cache->mmap);

    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i s:I s:i s:O}",
                           "count", zhashx_size (cache->entries),
                           "valid", cache->acct_valid,
                           "dirty", cache->acct_dirty,
                           "size", cache->acct_size,
                           "flush-batch-count", cache->flush_batch_count,
                           "mmap", o ? o : json_null ()) < 0)
        flux_log_error (h, "content stats");
    json_decref (o);
}

/* Handle request to store all dirty entries.  The store requests are batched
 * and handled asynchronously.  flush_respond() may be called immediately
 * if there are no dirty entries, or later from cache_resume_flush().
 * If 'backing' is false on rank 0, we go ahead and try to issue the store
 * requests and handle the ENOSYS errors that result.
 */

/* This is called when outstanding store ops have completed.  */
static void flush_respond (struct content_cache *cache)
{
    if (!cache->acct_dirty) {
        request_list_respond_raw (&cache->flush_requests,
                                  cache->h,
                                  0,
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

static void content_flush_request (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct content_cache *cache = arg;

    if (cache->rank == 0 && !cache->backing) {
        errno = ENOSYS;
        goto error;
    }
    if (cache->acct_dirty > 0) {
        if (cache_flush (cache) < 0)
            goto error;
        /* if flush_batch_count == 0, no stores are in progress.  If
         * there is a problem with the backing store, we must return
         * error here.  We assume that the last store error is the
         * primary storage error (i.e. ENOSPC, ENOSYS, etc.).
         */
        if (!cache->flush_batch_count && cache->flush_errno) {
            errno = cache->flush_errno;
            goto error;
        }
        if (msgstack_push (&cache->flush_requests, msg) < 0)
            goto error;
        return;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to content flush");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to content flush");
}

/* Heartbeat drives periodic cache purge
 */

static void cache_purge (struct content_cache *cache)
{
    double now = flux_reactor_now (cache->reactor);
    struct cache_entry *e = NULL;
    struct cache_entry *next;

    list_for_each_rev_safe (&cache->lru, e, next, list) {
        if (cache->acct_size <= cache->purge_target_size
            || now - e->lastused < cache->purge_old_entry)
            break;
        assert (e->valid);
        assert (!e->dirty);
        cache_entry_remove (cache, e);
    }
}

static void update_stats (struct content_cache *cache)
{
    flux_stats_gauge_set (cache->h, "content-cache.count",
        (int) zhashx_size (cache->entries));
    flux_stats_gauge_set (cache->h, "content-cache.valid",
        cache->acct_valid);
    flux_stats_gauge_set (cache->h, "content-cache.dirty",
        cache->acct_dirty);
    flux_stats_gauge_set (cache->h, "content-cache.size",
        cache->acct_size);
    flux_stats_gauge_set (cache->h, "content-cache.flush-batch-count",
        cache->flush_batch_count);
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct content_cache *cache = arg;

    if (flux_stats_enabled (cache->h, NULL))
        update_stats (cache);

    cache_purge (cache);

    flux_future_reset (f);
}

bool content_cache_backing_loaded (struct content_cache *cache)
{
    return cache->backing;
}

/* Initialization
 */

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content.load",
        content_load_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.store",
        content_store_request,
        0
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
        "content.stats-get",
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

static int get_hash_name (struct content_cache *cache)
{
    const char *s;

    if (!(s = flux_attr_get (cache->h, "content.hash"))) {
        if (flux_attr_set (cache->h, "content.hash", default_hash) < 0) {
            flux_log_error (cache->h, "setattr content.hash");
            return -1;
        }
        s = default_hash;
    }
    if ((content_hash_size = blobref_validate_hashtype (s)) < 0
        || !(cache->hash_name = strdup (s))) {
        flux_log_error (cache->h, "%s: unknown hash type", s);
        return -1;
    }
    return 0;
}

static int parse_u32 (const char *s, uint32_t *val)
{
    unsigned long u;
    char *endptr;

    errno = 0;
    u = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || u > UINT32_MAX)
        return -1;
    *val = u;
    return 0;
}

int parse_args (struct content_cache *cache, int argc, char **argv)
{
    uint32_t val;

    for (int i = 0; i < argc; i++) {
        if (strstarts (argv[i], "purge-target-size=")) {
            if (parse_u32 (argv[i] + 18, &val) < 0) {
                flux_log (cache->h, LOG_ERR, "error parsing %s", argv[i]);
                return -1;
            }
            cache->purge_target_size = val;
        }
        else if (strstarts (argv[i], "purge-old-entry=")) {
            if (parse_u32 (argv[i] + 16, &val) < 0) {
                flux_log (cache->h, LOG_ERR, "error parsing %s", argv[i]);
                return -1;
            }
            cache->purge_old_entry = val;
        }
        else if (strstarts (argv[i], "flush-batch-limit=")) {
            if (parse_u32 (argv[i] + 18, &val) < 0) {
                flux_log (cache->h, LOG_ERR, "error parsing %s", argv[i]);
                return -1;
            }
            cache->flush_batch_limit = val;
        }
        else if (strstarts (argv[i], "blob-size-limit=")) {
            if (parse_u32 (argv[i] + 16, &val) < 0) {
                flux_log (cache->h, LOG_ERR, "error parsing %s", argv[i]);
                return -1;
            }
            cache->blob_size_limit = val;
        }
        else {
            flux_log (cache->h, LOG_ERR, "unknown module option: %s", argv[i]);
            return -1;
        }
    }
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
        content_checkpoint_destroy (cache->checkpoint);
        content_mmap_destroy (cache->mmap);
        free (cache->hash_name);
        free (cache);
        errno = saved_errno;
    }
}

struct content_cache *content_cache_create (flux_t *h, int argc, char **argv)
{
    struct content_cache *cache;

    if (!(cache = calloc (1, sizeof (*cache))))
        return NULL;
    if (!(cache->entries = zhashx_new ()))
        goto nomem;
    cache->h = h;
    cache->reactor = flux_get_reactor (h);

    zhashx_set_destructor (cache->entries, cache_entry_destructor);
    zhashx_set_key_hasher (cache->entries, cache_entry_hasher);
    zhashx_set_key_comparator (cache->entries, cache_entry_comparator);
    zhashx_set_key_destructor (cache->entries, NULL); // key is part of entry
    zhashx_set_key_duplicator (cache->entries, NULL); // key is part of entry

    cache->rank = FLUX_NODEID_ANY;
    cache->blob_size_limit = default_blob_size_limit;
    cache->flush_batch_limit = default_flush_batch_limit;
    cache->purge_target_size = default_cache_purge_target_size;
    cache->purge_old_entry = default_cache_purge_old_entry;
    /* Some tunables may be set on the module command line (mainly for test).
     */
    if (parse_args (cache, argc, argv) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (get_hash_name (cache) < 0)
        goto error;

    list_head_init (&cache->lru);
    list_head_init (&cache->flush);

    if (flux_get_rank (h, &cache->rank) < 0)
        goto error;

    if (!(cache->checkpoint = content_checkpoint_create (h,
                                                         cache->rank,
                                                         cache)))
        goto error;
    if (cache->rank == 0) {
        if (!(cache->mmap = content_mmap_create (h,
                                                 cache->hash_name,
                                                 content_hash_size)))
            goto error;
    }
    if (flux_msg_handler_addvec (h, htab, cache, &cache->handlers) < 0)
        goto error;
    if (!(cache->f_sync = flux_sync_create (h, 0))
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
