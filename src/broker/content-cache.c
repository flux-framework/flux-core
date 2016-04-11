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

/* See RFC 10 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/sha1.h"
#include "src/common/libutil/shastring.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/iterators.h"

#include "attr.h"
#include "content-cache.h"

static const uint32_t default_cache_purge_target_entries = 1024*1024;
static const uint32_t default_cache_purge_target_size = 1024*1024*16;

static const uint32_t default_cache_purge_old_entry = 5;
static const uint32_t default_cache_purge_large_entry = 256;

/* Raise the max blob size value to 1GB so that large KVS values
 * (including KVS directories) can be supported while the KVS transitions
 * to the RFC 11 treeobj data representation.
 */
//static const uint32_t default_blob_size_limit = 1048576; /* RFC 10 */
static const uint32_t default_blob_size_limit = 1048576*1024;

static const uint32_t default_flush_batch_limit = 256;


struct cache_entry {
    void *data;
    int len;
    char *blobref;
    uint8_t valid:1;                /* entry contains valid data */
    uint8_t dirty:1;                /* entry needs to be stored upstream */
                                    /*   or to backing store (rank 0) */
    uint8_t load_pending:1;
    uint8_t store_pending:1;
    zlist_t *load_requests;
    zlist_t *store_requests;
    int lastused;
};

struct content_cache {
    flux_t h;
    flux_t enclosing_h;
    uint32_t rank;
    zhash_t *entries;
    uint8_t backing:1;              /* 'content-backing' service available */
    char *backing_name;
    char *hash_name;
    zlist_t *flush_requests;
    int epoch;

    uint32_t blob_size_limit;
    uint32_t flush_batch_limit;
    uint32_t flush_batch_count;

    uint32_t purge_target_entries;
    uint32_t purge_target_size;
    uint32_t purge_old_entry;
    uint32_t purge_large_entry;

    uint32_t acct_size;             /* total size of all cache entries */
    uint32_t acct_valid;            /* count of valid cache entries */
    uint32_t acct_dirty;            /* count of dirty cache entries */
};

static void flush_respond (content_cache_t *cache);
static int cache_flush (content_cache_t *cache);

static void message_list_destroy (zlist_t **l)
{
    flux_msg_t *msg;
    if (*l) {
        while ((msg = zlist_pop (*l)))
            flux_msg_destroy (msg);
        zlist_destroy (l);
    }
}

/* Respond identically to a list of requests.
 * The list is always run to completion, then destroyed.
 * Returns 0 on succes, -1 on failure with errno set.
 */
static int respond_requests_raw (zlist_t **l, flux_t h, int errnum,
                                 const void *data, int len)
{
    flux_msg_t *msg;
    int rc = 0, saved_errno = 0;

    if (*l) {
        while ((msg = zlist_pop (*l))) {
            if (flux_respond_raw (h, msg, errnum, data, len) < 0) {
                saved_errno = errno;
                rc = -1;
            }
            flux_msg_destroy (msg);
        }
        zlist_destroy (l);
    }
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

/* Add request message to a list, creating the list as needed.
 * Returns 0 on succes, -1 on failure with errno set.
 */
static int defer_request (zlist_t **l, const flux_msg_t *msg)
{
    flux_msg_t *cpy = NULL;

    if (!*l) {
        if (!(*l = zlist_new ()))
            goto nomem;
    }
    if (!(cpy = flux_msg_copy (msg, false)))
        goto error;
    if (zlist_append (*l, cpy) < 0)
        goto nomem;
    return 0;
nomem:
    errno = ENOMEM;
error:
    flux_msg_destroy (cpy);
    return -1;
}

/* Destroy a cache entry
 */
static void cache_entry_destroy (void *arg)
{
    struct cache_entry *e = arg;
    if (e) {
        if (e->data)
            free (e->data);
        if (e->blobref)
            free (e->blobref);
        assert (!e->load_requests || zlist_size (e->load_requests) == 0);
        assert (!e->store_requests || zlist_size (e->store_requests) == 0);
        message_list_destroy (&e->load_requests);
        message_list_destroy (&e->store_requests);
        free (e);
    }
}

/* Create a cache entry.
 * Initially only the digest is filled in;  defaults for the rest (zeroed).
 * Returns entry on success, NULL with errno set on failure.
 */
static struct cache_entry *cache_entry_create (const char *blobref)
{
    struct cache_entry *e = malloc (sizeof (*e));
    if (!e) {
        errno = ENOMEM;
        return NULL;
    }
    memset (e, 0, sizeof (*e));
    if (!(e->blobref = strdup (blobref))) {
        free (e);
        errno = ENOMEM;
        return NULL;
    }
    return e;
}

/* Make an invalid cache entry valid, filling in its data.
 * Returns 0 on success, -1 on failure with errno set.
 */
static int cache_entry_fill (struct cache_entry *e, const void *data, int len)
{
    int rc = -1;

    if (!e->valid) {
        assert (!e->data);
        assert (e->len == 0);
        if (len > 0 && !(e->data = malloc (len))) {
            errno = ENOMEM;
            goto done;
        }
        memcpy (e->data, data, len);
        e->len = len;
    }
    rc = 0;
done:
    return rc;
}

/* Insert a cache entry, by blobref.
 * Returns 0 on success, -1 on failure with errno set.
 * Side effect: destroys entry on failure.
 */
static int insert_entry (content_cache_t *cache, struct cache_entry *e)
{
    if (zhash_insert (cache->entries, e->blobref, e) < 0) {
        cache_entry_destroy (e);
        errno = ENOMEM;
        return -1;
    }
    zhash_freefn (cache->entries, e->blobref, cache_entry_destroy);
    if (e->valid) {
        cache->acct_size += e->len;
        cache->acct_valid++;
    }
    if (e->dirty)
        cache->acct_dirty++;
    return 0;
}

/* Look up a cache entry, by blobref.
 * Returns entry on success, NULL on failure.
 * N.B. errno is not set
 */
static struct cache_entry *lookup_entry (content_cache_t *cache,
                                         const char *blobref)
{
    return zhash_lookup (cache->entries, blobref);
}

/* Remove a cache entry.
 */
static void remove_entry (content_cache_t *cache, struct cache_entry *e)
{
    if (e->valid) {
        cache->acct_size -= e->len;
        cache->acct_valid--;
    }
    if (e->dirty)
        cache->acct_dirty--;
    zhash_delete (cache->entries, e->blobref);
}

/* Load operation
 *
 * If a cache entry is already present and valid, response is immediate.
 * Otherwise request is queued on the invalid cache entry, and a new
 * request is sent to the next level of TBON, or on rank 0, to the
 * content-backing service.  At most a single request is sent per cache entry.
 * Once the response is received, identical responses are sent to all
 * parked requests, and cache entry is made valid or removed if there was
 * an error such as ENOENT.
 */

static void cache_load_continuation (flux_rpc_t *rpc, void *arg)
{
    content_cache_t *cache = arg;
    struct cache_entry *e = flux_rpc_aux_get (rpc);
    void *data = NULL;
    int len = 0;
    int saved_errno;
    int rc = -1;

    e->load_pending = 0;
    if (flux_rpc_get_raw (rpc, NULL, &data, &len) < 0) {
        if (errno == ENOSYS && cache->rank == 0)
            errno = ENOENT;
        saved_errno = errno;
        if (errno != ENOENT)
            flux_log_error (cache->h, "content load");
        goto done;
    }
    if (cache_entry_fill (e, data, len) < 0) {
        saved_errno = errno;
        flux_log_error (cache->h, "content load");
        goto done;
    }
    if (!e->valid) {
        e->valid = 1;
        cache->acct_valid++;
        cache->acct_size += len;
    }
    e->lastused = cache->epoch;
    rc = 0;
done:
    if (respond_requests_raw (&e->load_requests, cache->h,
                                                    rc < 0 ? saved_errno : 0,
                                                    e->data, e->len) < 0)
        flux_log_error (cache->h, "%s: error responding to load requests",
                        __FUNCTION__);
    if (rc < 0)
        remove_entry (cache, e);
    flux_rpc_destroy (rpc);
}

static int cache_load (content_cache_t *cache, struct cache_entry *e)
{
    flux_rpc_t *rpc;
    int saved_errno = 0;
    int rc = -1;

    if (e->load_pending)
        return 0;
    if (cache->rank > 0) {
        rpc = flux_rpc_raw (cache->h, "content.load",
                            e->blobref, strlen (e->blobref) + 1,
                            FLUX_NODEID_UPSTREAM, 0);
    } else {
        rpc = flux_rpc_raw (cache->h, "content-backing.load",
                            e->blobref, strlen (e->blobref) + 1,
                            FLUX_NODEID_ANY, 0);
    }
    if (!rpc) {
        if (errno == ENOSYS && cache->rank == 0)
            errno = ENOENT;
        saved_errno = errno;
        if (errno != ENOENT)
            flux_log_error (cache->h, "%s: RPC", __FUNCTION__);
        goto done;
    }
    flux_rpc_aux_set (rpc, e, NULL);
    if (flux_rpc_then (rpc, cache_load_continuation, cache) < 0) {
        saved_errno = errno;
        flux_log_error (cache->h, "content load");
        goto done;
    }
    e->load_pending = 1;
    rc = 0;
done:
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

void content_load_request (flux_t h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    const char *blobref;
    int blobref_size;
    void *data = NULL;
    int len = 0;
    struct cache_entry *e;
    int saved_errno = 0;
    int rc = -1;

    if (flux_request_decode_raw (msg, NULL, &blobref, &blobref_size) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        saved_errno = errno = EPROTO;
        goto done;
    }
    if (!(e = lookup_entry (cache, blobref))) {
        if (cache->rank == 0 && !cache->backing) {
            saved_errno = errno = ENOENT;
            goto done;
        }
        if (!(e = cache_entry_create (blobref))
                                            || insert_entry (cache, e) < 0) {
            saved_errno = errno;
            flux_log_error (h, "content load");
            goto done; /* insert destroys 'e' on failure */
        }
    }
    if (!e->valid) {
        if (cache_load (cache, e) < 0) {
            saved_errno = errno;
            goto done;
        }
        if (defer_request (&e->load_requests, msg) < 0) {
            saved_errno = errno;
            flux_log_error (h, "content load");
            goto done;
        }
        return; /* RPC continuation will respond to msg */
    }
    e->lastused = cache->epoch;
    data = e->data;
    len = e->len;
    rc = 0;
done:
    assert (rc == 0 || saved_errno != 0);
    if (flux_respond_raw (h, msg, rc < 0 ? saved_errno : 0,
                                                        data, len) < 0)
        flux_log_error (h, "content load");
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
 * even though the entry may be dirty with respect to a 'content-backing'
 * service.  This allows the cache to be updated at memory speeds,
 * while holding the invariant that after a store RPC returns, the entry may
 * be loaded from any rank.  The optional content-backing service can
 * offload rank 0 hash entries at a slower pace.
 */

static void cache_store_continuation (flux_rpc_t *rpc, void *arg)
{
    content_cache_t *cache = arg;
    struct cache_entry *e = flux_rpc_aux_get (rpc);
    const char *blobref;
    int blobref_size;
    int saved_errno = 0;
    int rc = -1;

    e->store_pending = 0;
    assert (cache->flush_batch_count > 0);
    cache->flush_batch_count--;
    if (flux_rpc_get_raw (rpc, NULL, &blobref, &blobref_size) < 0) {
        saved_errno = errno;
        if (cache->rank == 0 && errno == ENOSYS)
            flux_log (cache->h, LOG_DEBUG, "content store: %s",
                      "backing store service unavailable");
        else
            flux_log_error (cache->h, "content store");
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        saved_errno = errno = EPROTO;
        flux_log (cache->h, LOG_ERR, "content store: bad blobref");
        goto done;
    }
    if (strcmp (blobref, e->blobref)) {
        saved_errno = errno = EIO;
        flux_log (cache->h, LOG_ERR, "content store: wrong blobref");
        goto done;
    }
    if (e->dirty) {
        cache->acct_dirty--;
        e->dirty = 0;
    }
    rc = 0;
done:
    if (respond_requests_raw (&e->store_requests, cache->h,
                                        rc < 0 ? saved_errno : 0,
                                        blobref, blobref_size) < 0)
        flux_log_error (cache->h, "%s: error responding to store requests",
                        __FUNCTION__);
    flux_rpc_destroy (rpc);

    /* If cache has been flushed, respond to flush requests, if any.
     * If there are still dirty entries and the number of outstanding
     * store requests would not exceed the limit, flush more entries.
     * Optimization: since scanning for dirty entries is a linear search,
     * only do it when the number of outstanding store requests falls to
     * a low water mark, here hardwired to be half of the limit.
     */
    if (cache->acct_dirty == 0 || (cache->rank == 0 && !cache->backing))
        flush_respond (cache);
    else if (cache->acct_dirty - cache->flush_batch_count > 0
            && cache->flush_batch_count <= cache->flush_batch_limit / 2)
        (void)cache_flush (cache); /* resume flushing */
}

static int cache_store (content_cache_t *cache, struct cache_entry *e)
{
    flux_rpc_t *rpc;
    int saved_errno = 0;
    int rc = -1;

    assert (e->valid);

    if (e->store_pending)
        return 0;
    if (cache->rank > 0) {
        rpc = flux_rpc_raw (cache->h, "content.store",
                            e->data, e->len, FLUX_NODEID_UPSTREAM, 0);
    } else {
        if (cache->flush_batch_count >= cache->flush_batch_limit)
            return 0;
        rpc = flux_rpc_raw (cache->h, "content-backing.store",
                            e->data, e->len, FLUX_NODEID_ANY, 0);
    }
    if (!rpc) {
        saved_errno = errno;
        flux_log_error (cache->h, "content store");
        goto done;
    }
    flux_rpc_aux_set (rpc, e, NULL);
    if (flux_rpc_then (rpc, cache_store_continuation, cache) < 0) {
        saved_errno = errno;
        flux_log_error (cache->h, "content store");
        goto done;
    }
    e->store_pending = 1;
    cache->flush_batch_count++;
    rc = 0;
done:
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void content_store_request (flux_t h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    void *data;
    int len;
    struct cache_entry *e = NULL;
    SHA1_CTX sha1_ctx;
    uint8_t hash[SHA1_DIGEST_SIZE];
    char blobref[SHA1_STRING_SIZE];
    int rc = -1;

    if (flux_request_decode_raw (msg, NULL, &data, &len) < 0)
        goto done;
    if (len > cache->blob_size_limit) {
        errno = EFBIG;
        goto done;
    }
    SHA1_Init (&sha1_ctx);
    SHA1_Update (&sha1_ctx, (uint8_t *)data, len);
    SHA1_Final (&sha1_ctx, hash);
    sha1_hashtostr (hash, blobref);

    if (!(e = lookup_entry (cache, blobref))) {
        if (!(e = cache_entry_create (blobref)))
            goto done;
        if (insert_entry (cache, e) < 0)
            goto done; /* insert destroys 'e' on failure */
    }
    if (!e->valid) {
        if (cache_entry_fill (e, data, len) < 0)
            goto done;
        if (!e->valid) {
            e->valid = 1;
            cache->acct_valid++;
            cache->acct_size += len;
        }
        if (respond_requests_raw (&e->load_requests, cache->h, 0,
                                                        e->data, e->len) < 0)
            flux_log_error (cache->h, "%s: error responding to load requests",
                            __FUNCTION__);
        if (!e->dirty) {
            e->dirty = 1;
            cache->acct_dirty++;
        }
    }
    e->lastused = cache->epoch;
    if (e->dirty) {
        if (cache->rank > 0 || cache->backing) {
            if (cache_store (cache, e) < 0)
                goto done;
            if (cache->rank > 0) {  /* write-through */
                if (defer_request (&e->store_requests, msg) < 0)
                    goto done;
                return;
            }
        }
    } else {
        /* When a backing store module is unloaded, it will clear
         * cache->backing then attempt to store all its blobs.  Any of
         * those still in cache need to be marked dirty.
         */
        if (cache->rank == 0 && !cache->backing) {
            e->dirty = 1;
            cache->acct_dirty++;
        }
    }
    rc = 0;
done:
    assert (rc == 0 || errno != 0);
    if (flux_respond_raw (h, msg, rc < 0 ? errno : 0,
                                            blobref, SHA1_STRING_SIZE) < 0)
        flux_log_error (h, "content store");
}

/* Backing store is enabled/disabled by modules that provide the
 * 'content-backing' service.  At module load time, the backing module
 * informs the content service of its availability, and entries are
 * asynchronously duplicated on the backing store and made eligible for
 * dropping from the rank 0 cache.
 *
 * At module unload time, Backing store is disabled and content-backing
 * synchronously transfers content back to the cache.  This allows the
 * module providing the backing store to be replaced early at runtime,
 * before the amount of content exceeds the cache's ability to hold it.
 *
 * If the broker is being shutdown, this transfer is skipped by
 * to avoid unnecessary and possibly OOM-triggering data movement into
 * the cache.
 */

static int cache_flush (content_cache_t *cache)
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
    FOREACH_ZHASH (cache->entries, key, e) {
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

static void content_backing_request (flux_t h, flux_msg_handler_t *w,
                                     const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    const char *json_str;
    const char *name;
    JSON in = NULL;
    int rc = -1;
    bool backing;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (cache->rank != 0) {
        errno = EINVAL;
        goto done;
    }
    if (!(in = Jfromstr (json_str)) || !Jget_bool (in, "backing", &backing)
                                    || !Jget_str (in, "name", &name)) {
        errno = EPROTO;
        goto done;
    }
    if (!cache->backing && backing) {
        cache->backing = 1;
        cache->backing_name = xstrdup (name);
        flux_log (h, LOG_DEBUG,
                "content backing store: enabled %s", name);
        (void)cache_flush (cache);
    } else if (cache->backing && !backing) {
        cache->backing = 0;
        if (cache->backing_name)
            free (cache->backing_name);
        cache->backing_name = NULL;
        flux_log (h, LOG_DEBUG, "content backing store: disabled %s", name);
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "content backing");
    Jput (in);
};

/* Forcibly drop all entries from the cache that can be dropped
 * without data loss.
 * N.B. this walks the entire cache in one go.
 */

static void content_dropcache_request (flux_t h, flux_msg_handler_t *w,
                                       const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    zlist_t *keys = NULL;
    char *key;
    struct cache_entry *e;
    int orig_size;
    int saved_errno;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    orig_size = zhash_size (cache->entries);
    if (!(keys = zhash_keys (cache->entries))) {
        errno = ENOMEM;
        goto done;
    }
    while ((key = zlist_pop (keys))) {
        e = zhash_lookup (cache->entries, key);
        assert (e != NULL);
        if (e->valid && !e->dirty)
            remove_entry (cache, e);
        free (key);
    }
    rc = 0;
done:
    saved_errno = errno;
    if (rc < 0)
        flux_log (h, LOG_DEBUG, "content dropcache: %s",
                  strerror (saved_errno));
    else
        flux_log (h, LOG_DEBUG, "content dropcache %d/%d",
                  orig_size - (int)zhash_size (cache->entries), orig_size);
    errno = saved_errno;
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "content dropcache");
    zlist_destroy (&keys);
}

/* Return stats about the cache.
 */

static void content_stats_request (flux_t h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    JSON out = Jnew ();
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    Jadd_int (out, "count", zhash_size (cache->entries));
    Jadd_int (out, "valid", cache->acct_valid);
    Jadd_int (out, "dirty", cache->acct_dirty);
    Jadd_int (out, "size", cache->acct_size);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, Jtostr (out)) < 0)
        flux_log_error (h, "content stats");
    Jput (out);
}

/* Flush all dirty entries by walking the entire cache, issuing store
 * requests for all dirty entries.  Responses are handled asynchronously
 * using RPC continuations.  A response to the flush request is not sent
 * until all the store responses are received.  If 'backing' is false on
 * rank 0, we go ahead and issue the store requests and handle the ENOSYS
 * errors that result.
 */

/* This is called when outstanding store ops have completed.  */
static void flush_respond (content_cache_t *cache)
{
    int errnum = 0;

    if (cache->acct_dirty){
        errnum = EIO;
        if (cache->rank == 0 && !cache->backing)
            errnum = ENOSYS;
    }
    if (respond_requests_raw (&cache->flush_requests, cache->h,
                              errnum, NULL, 0) < 0)
        flux_log_error (cache->h, "%s: error responding to flush requests",
                        __FUNCTION__);
}

static void content_flush_request (flux_t h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;
    int saved_errno;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    if (cache->acct_dirty == 0) {
        rc = 0;
        goto done;
    }
    if (cache_flush (cache) < 0)
        goto done;
    if (cache->acct_dirty > 0) {
        if (defer_request (&cache->flush_requests, msg) < 0)
            goto done;
        return;
    }
    if (cache->acct_dirty > 0) {
        errno = EIO;
        goto done;
    }
    rc = 0;
done:
    saved_errno = errno;
    if (rc < 0)
        flux_log (h, LOG_DEBUG, "content flush: %s",
                  strerror (saved_errno));
    else
        flux_log (h, LOG_DEBUG, "content flush");
    errno = saved_errno;
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "content flush");
}

/* Heartbeat drives periodic cache purge
 */

static int cache_purge (content_cache_t *cache)
{
    int after_entries = zhash_size (cache->entries);
    int after_size = cache->acct_size;
    struct cache_entry *e;
    zlist_t *purge = NULL;
    int rc = -1;
    const char *key;

    if (cache->acct_dirty == zhash_size (cache->entries))
        return 0;

    FOREACH_ZHASH (cache->entries, key, e) {
        if (after_size <= cache->purge_target_size
                        && after_entries <= cache->purge_target_entries)
            break;
        if (!e->valid || e->dirty)
            continue;
        if (cache->epoch - e->lastused < cache->purge_old_entry)
            continue;
        if (after_entries <= cache->purge_target_entries
                    && e->len < cache->purge_large_entry)
            continue;
        if ((!purge && !(purge = zlist_new ()))
                    || zlist_append (purge, e) < 0) {
            errno = ENOMEM;
            goto done;
        }
        after_size -= e->len;
        after_entries--;
    }
    if (purge) {
        flux_log (cache->h, LOG_DEBUG, "content purge: %d entries",
                  (int)zlist_size (purge));
        while ((e = zlist_pop (purge)))
            remove_entry (cache, e);
    }
    rc = 0;
done:
    zlist_destroy (&purge);
    return rc;
}

static void heartbeat_event (flux_t h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    content_cache_t *cache = arg;

    if (flux_heartbeat_decode (msg, &cache->epoch) < 0)
        return; /* ignore mangled heartbeat */
    cache_purge (cache);
}

/* Initialization
 */

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "content.load",      content_load_request },
    { FLUX_MSGTYPE_REQUEST, "content.store",     content_store_request },
    { FLUX_MSGTYPE_REQUEST, "content.backing",   content_backing_request},
    { FLUX_MSGTYPE_REQUEST, "content.dropcache", content_dropcache_request },
    { FLUX_MSGTYPE_REQUEST, "content.stats.get", content_stats_request },
    { FLUX_MSGTYPE_REQUEST, "content.flush",     content_flush_request },
    { FLUX_MSGTYPE_EVENT,   "hb",                heartbeat_event },
    FLUX_MSGHANDLER_TABLE_END,
};

int content_cache_set_flux (content_cache_t *cache, flux_t h)
{
    cache->h = h;

    if (flux_msg_handler_addvec (h, handlers, cache) < 0
                || flux_get_rank (h, &cache->rank) < 0)
        return -1;
    if (flux_event_subscribe (h, "hb") < 0)
        return -1;
    return 0;
}

void content_cache_set_enclosing_flux (content_cache_t *cache, flux_t h)
{
    cache->enclosing_h = h;
}

static int content_cache_getattr (const char *name, const char **val, void *arg)
{
    content_cache_t *cache = arg;
    static char s[32];

    if (!strcmp (name, "content-hash"))
        *val = cache->hash_name;
    else if (!strcmp (name, "content-backing"))
        *val = cache->backing_name;
    else if (!strcmp (name, "content-acct-entries")) {
        snprintf (s, sizeof (s), "%ld", zhash_size (cache->entries));
        *val = s;
    } else
        return -1;
    return 0;
}

int content_cache_register_attrs (content_cache_t *cache, attr_t *attr)
{
    /* Purge tunables
     */
    if (attr_add_active_uint32 (attr, "content-purge-target-entries",
                &cache->purge_target_entries, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-purge-target-size",
                &cache->purge_target_size, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-purge-old-entry",
                &cache->purge_old_entry, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-purge-large-entry",
                &cache->purge_large_entry, 0) < 0)
        return -1;
    /* Accounting numbers
     */
    if (attr_add_active_uint32 (attr, "content-acct-size",
                &cache->acct_size, FLUX_ATTRFLAG_READONLY) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-acct-dirty",
                &cache->acct_dirty, FLUX_ATTRFLAG_READONLY) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-acct-valid",
                &cache->acct_valid, FLUX_ATTRFLAG_READONLY) < 0)
        return -1;
    if (attr_add_active (attr, "content-acct-entries", FLUX_ATTRFLAG_READONLY,
                content_cache_getattr, NULL, cache) < 0)
        return -1;
    /* Misc
     */
    if (attr_add_active_uint32 (attr, "content-flush-batch-limit",
                &cache->flush_batch_limit, 0) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-blob-size-limit",
                &cache->blob_size_limit, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_active (attr, "content-hash", FLUX_ATTRFLAG_IMMUTABLE,
                content_cache_getattr, NULL, cache) < 0)
        return -1;
    if (attr_add_active (attr, "content-backing",FLUX_ATTRFLAG_READONLY,
                 content_cache_getattr, NULL, cache) < 0)
        return -1;
    if (attr_add_active_uint32 (attr, "content-flush-batch-count",
                &cache->flush_batch_count, 0) < 0)
        return -1;
    return 0;
}

void content_cache_destroy (content_cache_t *cache)
{
    if (cache) {
        if (cache->h) {
            (void)flux_event_unsubscribe (cache->h, "hb");
            flux_msg_handler_delvec (handlers);
        }
        if (cache->backing_name)
            free (cache->backing_name);
        zhash_destroy (&cache->entries);
        message_list_destroy (&cache->flush_requests);
        free (cache);
    }
}

content_cache_t *content_cache_create (void)
{
    content_cache_t *cache = malloc (sizeof (*cache));
    if (!cache) {
        errno = ENOMEM;
        return NULL;
    }
    memset (cache, 0, sizeof (*cache));
    if (!(cache->entries = zhash_new ())) {
        content_cache_destroy (cache);
        errno = ENOMEM;
        return NULL;
    }
    cache->rank = FLUX_NODEID_ANY;
    cache->blob_size_limit = default_blob_size_limit;
    cache->flush_batch_limit = default_flush_batch_limit;
    cache->purge_target_entries = default_cache_purge_target_entries;
    cache->purge_target_size = default_cache_purge_target_size;
    cache->purge_old_entry = default_cache_purge_old_entry;
    cache->purge_large_entry = default_cache_purge_large_entry;
    cache->hash_name = "sha1";
    return cache;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
