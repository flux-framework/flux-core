/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* content-mmap.c - map files into content cache on rank 0
 *
 * Each file is represented by a 'struct content_region' that includes
 * a 'fileref' object containing the file's metadata and blobrefs for content.
 * The region also contains a pointer to mmap(2)ed memory for the file's
 * content.
 *
 * All files have one or more tags, so the regions are placed in a
 * hash-of-lists where the list names are tags, and the entries are struct
 * content_regions.  When files are mapped, the requestor provides a tag.
 * When files are removed, the requestor provides (only) one or more tags.
 *
 * The content-cache calls content_mmap_region_lookup() on rank 0 when it
 * doesn't have a requested blobref in cache, and only consults the backing
 * store when that fails.  If content_mmap_region_lookup() succeeds, the
 * content-cache takes a reference on the struct content_region.  When we
 * request to unmap a region, the munmap(2) and free of the struct is delayed
 * until all content-cache references are dropped.
 *
 * Slightly tricky optimization:
 * To speed up content_mmap_region_lookup() we have mm->cache, which is used
 * to find a content_region given a hash.  The cache contains hash keys for
 * mmapped data.  A given hash may appear in multiple files or parts of the
 * same file, so when a file is mapped, we put all its hashes in mm->cache
 * except those that are already mapped.  If nothing is unmapped, then we know
 * all the blobrefs for all the files will remain valid.  However when
 * something is unmapped we could be losing pieces of unrelated files.  Since
 * unmaps are bulk operations involving tags, we just walk the entire
 * hash-of-lists at that time and restore any missing cache entries.
 *
 * Safety issue:
 * The content addressable storage model relies on the fact that once hashed,
 * data does not change.  However, this cannot be assured when the data is
 * mmapped from a file that may not be protected from updates.  To avoid
 * propagating bad data in the cache, content_mmap_validate() is called each
 * time an mmapped cache entry is accessed.  This function recomputes the
 * hash to make sure the content has not changed.  If the data has changed,
 * the content-cache returns an error to the requestor.  In addition, mmapped
 * pages could become invalid if the size of a mapped file is reduced.
 * Accessing invalid pages could cause the broker to crash with SIGBUS.  To
 * mitigate this, content_mmap_validate() also calls stat(2) on the file to
 * make sure the memory region is still valid.  This is not foolproof because
 * it is inherently a time-of-check-time-of-use problem.  In fact we rate
 * limit the calls to stat(2) to avoid a "stat storm" when a file with many
 * blobrefs is accessed, which increases the window where it could have
 * changed.  But it's likely better than not checking at all.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <jansson.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/hola.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libfilemap/fileref.h"
#include "ccan/ptrint/ptrint.h"
#include "ccan/str/str.h"

#include "mmap.h"

struct cache_entry {
    struct content_region *reg;
    const void *data;                       // pointer into reg->mapinfo.base
    size_t size;                            //
    void *hash;                             // contiguous with struct
};

struct content_region {
    struct blobvec_mapinfo mapinfo;
    int refcount;
    json_t *fileref;

    struct content_mmap *mm;

    char *fullpath;                         // full path for stat(2) checking
    struct timespec last_check;             // rate limit stat(2) checking
};

struct content_mmap {
    flux_t *h;
    uint32_t rank;
    char *hash_name;
    flux_msg_handler_t **handlers;
    struct hola *tags;                      // tagged bundles of files/regions
    zhashx_t *cache;                        // hash digest => cache entry
};

static int content_hash_size;

static const double max_check_age = 5; // seconds since last region stat(2)

static void cache_entry_destroy (struct cache_entry *e)
{
    if (e) {
        int saved_errno = errno;
        free (e);
        errno = saved_errno;
    }
}

static struct cache_entry *cache_entry_create (const void *digest)
{
    struct cache_entry *e;

    if (!(e = calloc (1, sizeof (*e) + content_hash_size)))
        return NULL;
    e->hash = (char *)(e + 1);
    memcpy (e->hash, digest, content_hash_size);
    return e;
}

/* Add entry to cache.
 * If entry exists, return success.  The blobref must be valid in the
 * cache - where it comes from is unimportant.
 * N.B. this is a potential hot spot so defer memory allocation until
 * after lookup.
 */
static int cache_entry_add (struct content_region *reg,
                            const void *data,
                            size_t size,
                            const char *blobref)
{
    struct cache_entry *e;
    char digest[BLOBREF_MAX_DIGEST_SIZE];

    if (blobref_strtohash (blobref, digest, sizeof (digest)) < 0)
        return -1;
    if (zhashx_lookup (reg->mm->cache, digest) < 0)
        return 0;
    if (!(e = cache_entry_create (digest)))
        return -1;
    e->reg = reg;
    e->data = data;
    e->size = size;
    if (zhashx_insert (reg->mm->cache, e->hash, e) < 0) {
        cache_entry_destroy (e);
        return 0;
    }
    return 0;
}

/* Remove entry from cache IFF it belongs to this region.
 */
static int cache_entry_remove (struct content_mmap *mm,
                               struct content_region *reg,
                               const char *blobref)
{
    struct cache_entry *e;
    char digest[BLOBREF_MAX_DIGEST_SIZE];

    if (blobref_strtohash (blobref, digest, sizeof (digest)) < 0)
        return -1;
    if ((e = zhashx_lookup (mm->cache, digest)) // calls destructor
        && reg == e->reg)
        zhashx_delete (mm->cache, digest);
    return 0;
}

/* Remove all cache entries associated with region (blobvec + fileref).
 */
static void region_cache_remove (struct content_region *reg)
{
    if (reg->fileref) {
        int saved_errno = errno;
        const char *encoding = NULL;
        json_t *data = NULL;

        if (json_unpack (reg->fileref,
                         "{s?s s?o}",
                         "encoding", &encoding,
                         "data", &data) == 0
            && data != NULL
            && encoding != NULL
            && streq (encoding, "blobvec")) {
            size_t index;
            json_t *entry;

            json_array_foreach (data, index, entry) {
                json_int_t offset;
                json_int_t size;
                const char *blobref;

                if (json_unpack (entry,
                                 "[I,I,s]",
                                 &offset,
                                 &size,
                                 &blobref) == 0)
                    cache_entry_remove (reg->mm, reg, blobref);
            }
        }
        errno = saved_errno;
    }
}

/* Add cache entries for entries associated with region
 */
static int region_cache_add (struct content_region *reg)
{
    size_t index;
    const char *encoding = NULL;
    json_t *data = NULL;
    json_t *entry;

    if (json_unpack (reg->fileref,
                     "{s?s s?o}",
                     "encoding", &encoding,
                     "data", &data) < 0)
        goto inval;
    if (data && encoding && streq (encoding, "blobvec")) {
        json_array_foreach (data, index, entry) {
            json_int_t offset;
            json_int_t size;
            const char *blobref;
            if (json_unpack (entry, "[I,I,s]", &offset, &size, &blobref) < 0)
                goto inval;
            if (cache_entry_add (reg,
                                 reg->mapinfo.base + offset,
                                 size,
                                 blobref) < 0)
                return -1;
        }
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

/* After a region is unmapped, other regions may have blobrefs that are no
 * longer represented in the cache.  This scans all mapped regions and fills
 * in missing cache entries.  Design tradeoff:  mapping and lookup are fast,
 * and the cache implementation is lightweight and simple, at the expense of
 * unmap efficiency.
 */
static int plug_cache_holes (struct content_mmap *mm)
{
    const char *name;
    struct content_region *reg;

    name = hola_hash_first (mm->tags);
    while (name) {
        reg = hola_list_first (mm->tags, name);
        while (reg) {
            if (region_cache_add (reg) < 0)
                return -1;
            reg = hola_list_next (mm->tags, name);
        }
        name = hola_hash_next (mm->tags);
    }
    return 0;
}

// zhashx_destructor_fn footprint
static void cache_entry_destructor (void **item)
{
    if (item) {
        cache_entry_destroy (*item);
        *item = NULL;
    }
}
// zhashx_hash_fn footprint
static size_t cache_entry_hasher (const void *key)
{
    return *(size_t *)key;
}
// zhashx_comparator_fn footprint
static int cache_entry_comparator (const void *item1, const void *item2)
{
    return memcmp (item1, item2, content_hash_size);
}

void content_mmap_region_decref (struct content_region *reg)
{
    if (reg && --reg->refcount == 0) {
        int saved_errno = errno;
        region_cache_remove (reg);
        if (reg->mapinfo.base != MAP_FAILED)
            (void)munmap (reg->mapinfo.base, reg->mapinfo.size);
        json_decref (reg->fileref);
        free (reg->fullpath);
        free (reg);
        errno = saved_errno;
    }
}

// zhashx_destructor_fn footprint
static void content_mmap_region_destructor (void **item)
{
    if (item) {
        content_mmap_region_decref (*item);
        *item = NULL;
    }
}

struct content_region *content_mmap_region_incref (struct content_region *reg)
{
    if (reg)
        reg->refcount++;
    return reg;
}

/* Validate mmapped blob before use, checking for:
 * - size has changed so mmmapped pages are no longer valid (SIGBUS if used!)
 * - content no longer matches hash
 * To avoid repeatedly calling stat(2) on a file, skip it if last check was
 * within max_check_age seconds.
 */
bool content_mmap_validate (struct content_region *reg,
                            const void *hash,
                            int hash_size,
                            const void *data,
                            int data_size)
{
    char hash2[BLOBREF_MAX_DIGEST_SIZE];

    assert (reg->mapinfo.base != NULL);
    assert (data >= reg->mapinfo.base);
    assert (data + data_size <= reg->mapinfo.base + reg->mapinfo.size);

    if (monotime_since (reg->last_check)/1000 >= max_check_age) {
        struct stat sb;

        if (stat (reg->fullpath, &sb) < 0
            || sb.st_size < reg->mapinfo.size)
            return false;

        monotime (&reg->last_check);
    }

    if (blobref_hash_raw (reg->mm->hash_name,
                          data,
                          data_size,
                          hash2,
                          sizeof (hash2)) != hash_size
            || memcmp (hash, hash2, hash_size) != 0)
            return false;
    return true;
}

struct content_region *content_mmap_region_lookup (struct content_mmap *mm,
                                                   const void *hash,
                                                   int hash_size,
                                                   const void **data,
                                                   int *data_size)
{
    struct cache_entry *e;

    if (hash_size != content_hash_size
        || !(e = zhashx_lookup (mm->cache, hash)))
        return NULL;
    *data = e->data;
    *data_size = e->size;
    return e->reg;
}

static struct content_region *content_mmap_region_create (
                                                 struct content_mmap *mm,
                                                 const char *path,
                                                 int chunksize,
                                                 flux_error_t *error)
{
    struct blobvec_param param = {
        .hashtype = mm->hash_name,
        .chunksize = chunksize,
        .small_file_threshold = 0, // always choose blobvec encoding here
    };
    struct content_region *reg;

    if (!(reg = calloc (1, sizeof (*reg)))) {
        errprintf (error, "out of memory");
        goto error;
    }
    reg->refcount = 1;
    reg->mm = mm;
    reg->mapinfo.base = MAP_FAILED;
    if (!(reg->fullpath = strdup (path)))
        goto error;

    if (!(reg->fileref = fileref_create_ex (path,
                                            &param,
                                            &reg->mapinfo,
                                            error)))
        goto error;
    /* fileref_create_ex() accepts all file types, but flux-archive(1) should
     * not be requesting that files be mapped which do not meet criteria.
     */
    if (reg->mapinfo.base == MAP_FAILED) {
        errprintf (error, "%s: not suitable for mapping", path);
        errno = EINVAL;
        goto error;
    }
    if (region_cache_add (reg) < 0) {
        errprintf (error,
                   "%s: error caching region blobrefs: %s",
                   path,
                   strerror (errno));
        goto error;
    }
    return reg;
error:
    content_mmap_region_decref (reg);
    return NULL;
}

static void content_mmap_add_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    struct content_mmap *mm = arg;
    const char *path;
    int chunksize;
    const char *tag;
    struct content_region *reg = NULL;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i s:s}",
                             "path", &path,
                             "chunksize", &chunksize,
                             "tag", &tag) < 0)
        goto error;
    if (mm->rank != 0) {
        errmsg = "content may only be mmapped on rank 0";
        goto inval;
    }
    if (path[0] != '/') {
        errmsg = "path must be fully qualified";
        goto inval;
    }
    if (!(reg = content_mmap_region_create (mm, path, chunksize, &error))) {
        errmsg = error.text;
        goto error;
    }
    // takes a reference on region
    if (!hola_list_add_end (mm->tags, tag, reg))
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to content.mmap-add request");
    content_mmap_region_decref (reg);
    return;
inval:
    errno = EINVAL;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to content.mmap-add request");
    content_mmap_region_decref (reg);
}

static void content_mmap_remove_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct content_mmap *mm = arg;
    const char *errmsg = NULL;
    const char *tag;
    int unmap_count = 0;

    if (flux_request_unpack (msg, NULL, "{s:s}", "tag", &tag) < 0)
        goto error;
    if (mm->rank != 0) {
        errmsg = "content can only be mmapped on rank 0";
        goto inval;
    }
    if (hola_hash_delete (mm->tags, tag) == 0)
        unmap_count++;
    if (unmap_count > 0) {
        if (plug_cache_holes (mm) < 0) {
            errmsg = "error filling missing cache entries after unmap";
            goto error;
        }
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to content.mmap-remove request");
    return;
inval:
    errno = EINVAL;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to content.mmap-remove request");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content.mmap-add",
        content_mmap_add_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.mmap-remove",
        content_mmap_remove_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

json_t *content_mmap_get_stats (struct content_mmap *mm)
{
    const char *key;
    json_t *o;
    json_t *mmap;

    if (!mm)
        return NULL;
    if (!(o = json_object ()))
        goto nomem;
    key = hola_hash_first (mm->tags);
    while (key) {
        struct content_region *reg;
        json_t *a;
        if (!(a = json_array ()))
            goto nomem;
        reg = hola_list_first (mm->tags, key);
        while (reg) {
            json_t *s;
            if (!(s = json_string (reg->fullpath))
                || json_array_append_new (a, s) < 0) {
                json_decref (s);
                json_decref (a);
                goto nomem;
            }
            reg = hola_list_next (mm->tags, key);
        }
        if (json_object_set_new (o, key, a) < 0) {
            json_decref (a);
            goto nomem;
        }
        key = hola_hash_next (mm->tags);
    }
    if (!(mmap = json_pack ("{s:O s:I}",
                            "tags", o,
                            "blobs", zhashx_size (mm->cache))))
        goto nomem;
    json_decref (o);
    return mmap;
nomem:
    json_decref (o);
    return NULL;
}

void content_mmap_destroy (struct content_mmap *mm)
{
    if (mm) {
        int saved_errno = errno;
        flux_msg_handler_delvec (mm->handlers);
        hola_destroy (mm->tags);
        zhashx_destroy (&mm->cache);
        free (mm->hash_name);
        free (mm);
        errno = saved_errno;
    }
}

struct content_mmap *content_mmap_create (flux_t *h,
                                          const char *hash_name,
                                          int hash_size)
{
    struct content_mmap *mm;

    if (!(mm = calloc (1, sizeof (*mm))))
        return NULL;
    content_hash_size = hash_size;
    if (flux_get_rank (h, &mm->rank) < 0)
        goto error;
    if (!(mm->hash_name = strdup (hash_name)))
        goto error;
    mm->h = h;
    if (flux_msg_handler_addvec (h, htab, mm, &mm->handlers) < 0)
        goto error;
    if (!(mm->tags = hola_create (HOLA_AUTOCREATE)))
        goto error;
    hola_set_list_destructor (mm->tags, content_mmap_region_destructor);
    hola_set_list_duplicator (mm->tags,
                         (zlistx_duplicator_fn *)content_mmap_region_incref);
    if (!(mm->cache = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (mm->cache, cache_entry_destructor);
    zhashx_set_key_hasher (mm->cache, cache_entry_hasher);
    zhashx_set_key_comparator (mm->cache, cache_entry_comparator);
    zhashx_set_key_destructor (mm->cache, NULL); // key is part of entry
    zhashx_set_key_duplicator (mm->cache, NULL); // key is part of entry
    return mm;
error:
    content_mmap_destroy (mm);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
