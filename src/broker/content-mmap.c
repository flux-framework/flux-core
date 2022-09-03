/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* content-mmap.c - map files into content cache
 *
 * Purpose: leverage the hierarchical content cache for file broadcast.
 *
 * Before sending a load request to the backing store, the content cache on
 * rank 0 checks here to see if a blob can be pulled in from a mmapped file.
 *
 * A request to mmap a file returns an array of blobrefs which must be passed
 * to readers out of band.  Those blobs may be read through the cache to
 * reconstitute the original file at any broker, scalably.
 *
 * The file may be unmapped explicitly with a content.munmap request, or if the
 * "sticky" bit is was not set on the map request, may be unmapped when the
 * requestor disconnects.  The actual munmap(2) occurs once all blobs'
 * reference counts reach zero, indicating that any blobs in the rank 0 cache
 * that reference the mmapped region have been dropped from the cache.
 *
 * N.B. mmapped blobs are not written to the backing store; however, if a
 * blob is stored with the same hash as a mmapped blob, the blob then becomes
 * dirty in the cache and propagates to the backing store.  To facilitate this,
 * mmapped blobs are tracked in the cache with a special 'ephemeral' bit.
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
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/blobref.h"
#include "ccan/ptrint/ptrint.h"

#include "content-mmap.h"

struct content_region {
    char *path;
    void *data;
    off_t data_size;
    int refcount;
    int blob_size;
    int blob_count;
    void **hashes;
    zhashx_t *fast_lookup; // hash digest -> blob index (origin=1)
    struct content_mmap *mm;
};

struct content_mmap {
    flux_t *h;
    char *hash_name;
    flux_msg_handler_t **handlers;
    zhashx_t *regions;
};

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

static int content_hash_size;

static size_t fast_lookup_hasher (const void *key)
{
    return *(size_t *)key;
}
static int fast_lookup_comparator (const void *item1, const void *item2)
{
    return memcmp (item1, item2, content_hash_size);
}

void content_mmap_region_decref (struct content_region *reg)
{
    if (reg && --reg->refcount == 0) {
        int saved_errno = errno;
        if (reg->data != MAP_FAILED)
            (void)munmap (reg->data, reg->data_size);
        zhashx_destroy (&reg->fast_lookup);
        free (reg->path);
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

struct content_region *content_mmap_region_lookup (struct content_mmap *mm,
                                                   const void *hash,
                                                   int hash_len,
                                                   const void **data,
                                                   int *data_len)
{
    struct content_region *reg;

    assert (content_hash_size == hash_len);
    reg = zhashx_first (mm->regions);
    while (reg) {
        void *result;
        if ((result = zhashx_lookup (reg->fast_lookup, hash))) {
            off_t i = ptr2int (result) - 1;
            *data = (char *)reg->data + i*reg->blob_size;
            *data_len = MIN (reg->blob_size,
                             reg->data_size - i*reg->blob_size);
            return reg;
        }
        reg = zhashx_next (mm->regions);
    }
    return NULL;
}

static struct content_region *content_region_create (struct content_mmap *mm,
                                                     const char *path,
                                                     int blob_size)
{
    struct content_region *reg = NULL;
    int fd;
    struct stat sb;
    int blob_count;

    if ((fd = open (path, O_RDONLY)) < 0
        || fstat (fd, &sb) < 0)
        goto error;
    if (sb.st_size == 0) {
        errno = EINVAL;
        goto error;
    }
    blob_count = sb.st_size / blob_size;
    if (sb.st_size % blob_size > 0)
        blob_count++;

    if (!(reg = calloc (1, sizeof (*reg) + (blob_count * content_hash_size))))
        goto error;
    reg->refcount = 1;
    reg->mm = mm;
    reg->hashes = (void **)(reg + 1);
    reg->blob_count = blob_count;
    reg->blob_size = blob_size;
    reg->data_size = sb.st_size;
    reg->data = mmap (NULL, reg->data_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (reg->data == MAP_FAILED)
        goto error;
    if (!(reg->fast_lookup = zhashx_new ()))
        goto error;
    zhashx_set_key_hasher (reg->fast_lookup, fast_lookup_hasher);
    zhashx_set_key_comparator (reg->fast_lookup, fast_lookup_comparator);
    zhashx_set_key_duplicator (reg->fast_lookup, NULL);
    zhashx_set_key_destructor (reg->fast_lookup, NULL);
    for (off_t i = 0; i < blob_count; i++) {
        if (blobref_hash_raw (mm->hash_name,
                              (char *)reg->data + i*blob_size,
                              MIN (blob_size, reg->data_size - i*blob_size),
                              (char *)reg->hashes + i*content_hash_size,
                              content_hash_size) < 0)
            goto error;
        // ignore EEXIST
        (void)zhashx_insert (reg->fast_lookup,
                             (char *)reg->hashes + i*content_hash_size,
                             int2ptr (i + 1));
    }
    if (!(reg->path = strdup (path)))
        goto error;
    close (fd);
    return reg;
error:
    ERRNO_SAFE_WRAP (close, fd);
    content_mmap_region_decref (reg);
    return NULL;
}

static json_t *get_blobrefs (struct content_region *reg)
{
    struct content_mmap *mm = reg->mm;
    json_t *array;
    json_t *o;
    char blobref[BLOBREF_MAX_STRING_SIZE];

    if (!(array = json_array ()))
        goto nomem;
    for (int i = 0; i < reg->blob_count; i++) {
        if (blobref_hashtostr (mm->hash_name,
                               (char *)reg->hashes + i*content_hash_size,
                               content_hash_size,
                               blobref,
                               sizeof (blobref)) < 0)
            goto error;
        if (!(o = json_string (blobref))
            || json_array_append_new (array, o) < 0) {
            json_decref (o);
            goto nomem;
        }
    }
    return array;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, array);
    return NULL;
}

static void content_mmap_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct content_mmap *mm = arg;
    const char *path;
    int blob_size;
    struct content_region *reg;
    const char *errmsg = NULL;
    json_t *blobrefs;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i}",
                             "path", &path,
                             "blobsize", &blob_size) < 0)
        goto error;
    if (blob_size < 1) {
        errno = EINVAL;
        errmsg = "blob size must be > 0";
        goto error;
    }
    if (!(reg = content_region_create (mm, path, blob_size)))
        goto error;
    if (zhashx_insert (mm->regions, reg->path, reg) < 0) {
        content_mmap_region_decref (reg);
        errno = EEXIST;
        errmsg = "file is already mapped";
        goto error;
    }
    if (!(blobrefs = get_blobrefs (reg)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:O}", "blobrefs", blobrefs) < 0)
        flux_log_error (h, "error responding to content.mmap request");
    json_decref (blobrefs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to content.mmap request");
}

static void content_munmap_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct content_mmap *mm = arg;
    const char *path;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s}",
                             "path", &path) < 0)
        goto error;
    if (zhashx_lookup (mm->regions, path) < 0) {
        errno = ENOENT;
        goto error;
    }
    zhashx_delete (mm->regions, path);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to content.munmap request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to content.munmap request");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content.mmap",
        content_mmap_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.munmap",
        content_munmap_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void content_mmap_destroy (struct content_mmap *mm)
{
    if (mm) {
        int saved_errno = errno;
        flux_msg_handler_delvec (mm->handlers);
        zhashx_destroy (&mm->regions);
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
    if (!(mm->hash_name = strdup (hash_name)))
        goto error;
    content_hash_size = hash_size;
    mm->h = h;
    if (flux_msg_handler_addvec (h, htab, mm, &mm->handlers) < 0)
        goto error;
    if (!(mm->regions = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (mm->regions, content_mmap_region_destructor);
    zhashx_set_key_destructor (mm->regions, NULL);
    zhashx_set_key_duplicator (mm->regions, NULL);
    return mm;
error:
    content_mmap_destroy (mm);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
