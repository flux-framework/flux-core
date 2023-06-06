/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>
#include <jansson.h>
#include <archive.h>
#include <archive_entry.h>

#include "filemap.h"

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libcontent/content.h"
#include "src/common/libutil/fileref.h"

/* Decode the raw data field a fileref object, setting the result in 'data'
 * and 'data_size'.  Caller must free.
 */
static int decode_data (const char *s, void **data, size_t *data_size)
{
    if (s) {
        int len = strlen (s);
        size_t bufsize = base64_decoded_length (len);
        void *buf;
        ssize_t n;

        if (!(buf = malloc (bufsize)))
            return -1;
        if ((n = base64_decode (buf, bufsize, s, len)) < 0) {
            free (buf);
            errno = EINVAL;
            return -1;
        }
        *data = buf;
        *data_size = n;
    }
    return 0;
}

static json_t *load_fileref (flux_t *h,
                             const char *blobref,
                             flux_error_t *errp)
{
    flux_future_t *f;
    const void *buf;
    int size;
    json_t *o;
    json_error_t error;

    if (!(f = content_load_byblobref (h, blobref, 0))
        || content_load_get (f, &buf, &size) < 0) {
        errprintf (errp,
                   "error loading fileref from %s: %s",
                   blobref,
                   future_strerror (f, errno));
        return NULL;
    }
    if (!(o = json_loads (buf, 0, &error))) {
        errprintf (errp,
                   "error decoding fileref object from %s: %s",
                   blobref,
                   error.text);
        return NULL;
    }
    flux_future_destroy (f);
    return o;
}

flux_future_t *filemap_mmap_list (flux_t *h,
                                  bool blobref,
                                  json_t *tags,
                                  const char *pattern)
{
    flux_future_t *f;

    if (pattern) {
        f = flux_rpc_pack (h,
                           "content.mmap-list",
                           0,
                           FLUX_RPC_STREAMING,
                           "{s:b s:s s:O}",
                           "blobref", blobref ? 1 : 0,
                           "pattern", pattern,
                           "tags", tags);
    }
    else {
        f = flux_rpc_pack (h,
                           "content.mmap-list",
                           0,
                           FLUX_RPC_STREAMING,
                           "{s:b s:O}",
                           "blobref", blobref ? 1 : 0,
                           "tags", tags);
    }
    return f;
}

static int extract_blob (flux_t *h,
                         struct archive *archive,
                         const char *path,
                         json_t *o,
                         flux_error_t *errp)
{
    struct {
        json_int_t offset;
        json_int_t size;
        const char *blobref;
    } entry;
    flux_future_t *f;
    const void *buf;
    int size;

    if (json_unpack (o,
                     "[I,I,s]",
                     &entry.offset,
                     &entry.size,
                     &entry.blobref) < 0)
        return errprintf (errp, "%s: error decoding blobvec entry", path);
    if (!(f = content_load_byblobref (h, entry.blobref, 0))
        || content_load_get (f, &buf, &size) < 0) {
        return errprintf (errp,
                          "%s: error loading offset=%ju size=%ju from %s: %s",
                          path,
                          (uintmax_t)entry.offset,
                          (uintmax_t)entry.size,
                          entry.blobref,
                          future_strerror (f, errno));
    }
    if (size != entry.size) {
        return errprintf (errp,
                          "%s: error loading offset=%ju size=%ju from %s:"
                          " unexpected size %ju",
                          path,
                          (uintmax_t)entry.offset,
                          (uintmax_t)entry.size,
                          entry.blobref,
                          (uintmax_t)size);
    }
    if (archive_write_data_block (archive,
                                  buf,
                                  size,
                                  entry.offset) != ARCHIVE_OK)
        return errprintf (errp,
                          "%s: write: %s",
                          path,
                          archive_error_string (archive));
    flux_future_destroy (f);
    return 0;
}

/*  Extract a single file from a 'fileref' object using an existing
 *  libarchive object 'archive' and using 'path' as the default path
 *  if no path is encoded in 'fileref'.
 */
static int extract_file (flux_t *h,
                         struct archive *archive,
                         const char *path,
                         json_t *fileref,
                         flux_error_t *errp,
                         filemap_trace_f trace_cb,
                         void *arg)
{
    int mode;
    json_int_t size = -1;
    json_int_t ctime = -1;
    json_int_t mtime = -1;
    const char *encoding = NULL;
    json_t *data = NULL;
    size_t index;
    json_t *o;
    struct archive_entry *entry;
    json_error_t error;

    if (json_unpack_ex (fileref,
                        &error,
                        0,
                        "{s?s s:i s?I s?I s?I s?s s?o}",
                        "path", &path,
                        "mode", &mode,
                        "size", &size,
                        "mtime", &mtime,
                        "ctime", &ctime,
                        "encoding", &encoding,
                        "data", &data) < 0)
        return errprintf (errp,
                          "error decoding fileref object: %s",
                          error.text);

    if (trace_cb)
        (*trace_cb) (arg, fileref, path, mode, size, mtime, ctime, encoding);

    /* metadata
     */
    if (!(entry = archive_entry_new ()))
        return errprintf (errp, "%s: error creating libarchive entry", path);
    archive_entry_set_pathname (entry, path);
    archive_entry_set_mode (entry, mode);
    if (mtime != -1)
        archive_entry_set_mtime (entry, mtime, 0);
    if (ctime != -1)
        archive_entry_set_ctime (entry, ctime, 0);
    if (S_ISREG (mode)) {
        if (size != -1)
            archive_entry_set_size (entry, size);
    }
    else if (S_ISLNK (mode)) {
        const char *target;
        if (!data || !(target = json_string_value (data)))
            return errprintf (errp, "%s: missing symlink data", path);
        archive_entry_set_symlink (entry, target);
    }
    else if (!S_ISDIR (mode)) // nothing to do for directory
        return errprintf (errp,
                          "%s: unknown file type (mode=0%o)",
                          path,
                          mode);
    if (archive_write_header (archive, entry) != ARCHIVE_OK)
        return errprintf (errp,
                          "%s: %s",
                          path,
                          archive_error_string (archive));

    /* data
     */
    if (S_ISREG (mode) && data != NULL) {
        if (!encoding) {
          char *str;
            if (!(str = json_dumps (data, JSON_ENCODE_ANY | JSON_COMPACT)))
                return errprintf (errp,
                                  "%s: could not encode JSON file data",
                                  path);
            if (archive_write_data_block (archive,
                                          str,
                                          strlen (str),
                                          0) != ARCHIVE_OK) {
                return errprintf (errp,
                                  "%s: write: %s",
                                  path,
                                  archive_error_string (archive));
            }
            free (str);
        }
        else if (streq (encoding, "base64")) {
            const char *str = json_string_value (data);
            void *buf;
            size_t buf_size;

            if (!str || decode_data (str, &buf, &buf_size) < 0)
                return errprintf (errp,
                                  "%s: could not decode base64 file data",
                                  path);
            if (archive_write_data_block (archive,
                                          buf,
                                          buf_size,
                                          0) != ARCHIVE_OK) {
                return errprintf (errp,
                                  "%s: write: %s",
                                  path,
                                  archive_error_string (archive));
            }
            free (buf);
        }
        else if (streq (encoding, "blobvec")) {
            json_array_foreach (data, index, o) {
                if (extract_blob (h, archive, path, o, errp) < 0)
                    return -1;
            }
        }
        else if (streq (encoding, "utf-8")) {
            const char *str = json_string_value (data);
            if (!str)
                return errprintf (errp,
                                  "%s: unexpected data type for utf8",
                                  path);
            if (archive_write_data_block (archive,
                                          str,
                                          strlen (str),
                                          0) != ARCHIVE_OK) {
                return errprintf (errp,
                                  "%s: write: %s",
                                  path,
                                  archive_error_string (archive));
            }
        }
        else {
            return errprintf (errp,
                              "%s: unknown RFC 37 encoding %s",
                              path,
                              encoding);
        }
    }

    archive_entry_free (entry);
    return 0;
}

static int extract_fileref (flux_t *h,
                            const char *path,
                            json_t *fileref,
                            bool direct,
                            struct archive *archive,
                            flux_error_t *errp,
                            filemap_trace_f trace_cb,
                            void *arg)
{
    int rc;
    json_t *o = NULL;
    if (!direct) {
        if (!(o = load_fileref (h, json_string_value (fileref), errp)))
            return -1;
        fileref = o;
    }
    rc = extract_file (h, archive, path, fileref, errp, trace_cb, arg);
    json_decref (o);
    return rc;
}

int filemap_extract (flux_t *h,
                     json_t *files,
                     bool direct,
                     flux_error_t *errp,
                     filemap_trace_f trace_cb,
                     void *arg)
{
    const char *key;
    size_t index;
    json_t *entry;
    struct archive *archive;
    int rc = -1;

    if (!(archive = archive_write_disk_new ()))
        return errprintf (errp, "error creating libarchive context");

    if (json_is_array (files)) {
        json_array_foreach (files, index, entry) {
            if (extract_fileref (h,
                                 NULL,
                                 entry,
                                 direct,
                                 archive,
                                 errp,
                                 trace_cb,
                                 arg) < 0)
            goto out;
        }
    } else {
        json_object_foreach (files, key, entry) {
            if (extract_fileref (h,
                                 key,
                                 entry,
                                 direct,
                                 archive,
                                 errp,
                                 trace_cb,
                                 arg) < 0)
            goto out;
        }
    }
    rc = 0;
out:
    if (archive)
        archive_write_free (archive);
    return rc;
}

/* vi: ts=4 sw=4 expandtab
 */
