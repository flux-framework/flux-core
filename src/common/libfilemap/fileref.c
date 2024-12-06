/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* fileref.c - helpers for RFC 37 file system objects
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>
#include <assert.h>

#include "ccan/base64/base64.h"

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/fdutils.h"
#include "fileref.h"

static int blobvec_append (json_t *blobvec,
                           const void *mapbuf,
                           off_t offset,
                           size_t blobsize,
                           const char *hashtype)
{
    char blobref[BLOBREF_MAX_STRING_SIZE];
    json_t *o;
    json_int_t offsetj = offset;
    json_int_t blobsizej = blobsize;

    if (blobref_hash (hashtype,
                      mapbuf + offset,
                      blobsize,
                      blobref,
                      sizeof (blobref)) < 0)
        return -1;
    if (!(o = json_pack ("[I,I,s]", offsetj, blobsizej, blobref))
        || json_array_append_new (blobvec, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static bool file_has_no_data (int fd)
{
#ifdef SEEK_DATA
    if (lseek (fd, 0, SEEK_DATA) == (off_t)-1 && errno == ENXIO)
        return true;
#endif
    return false;
}

/* Walk the regular file represented by 'fd', appending blobvec array entries
 * to 'blobvec' array for each 'chunksize' region.  Use SEEK_DATA and SEEK_HOLE
 * to skip holes in sparse files - see lseek(2).
 */
static json_t *blobvec_create (int fd,
                               const void *mapbuf,
                               size_t size,
                               const char *hashtype,
                               size_t chunksize)
{
    json_t *blobvec;
    off_t offset = 0;

    assert (fd >= 0);
    assert (size > 0);

    if (!(blobvec = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    while (offset < size) {
#ifdef SEEK_DATA
        // N.B. fails with ENXIO if there is no more data
        if ((offset = lseek (fd, offset, SEEK_DATA)) == (off_t)-1) {
            if (errno == ENXIO)
                break;
            goto error;
        }
#endif
        if (offset < size) {
            off_t notdata;
            size_t blobsize;

#ifdef SEEK_HOLE
            // N.B. returns size if there are no more holes
            if ((notdata = lseek (fd, offset, SEEK_HOLE)) == (off_t)-1)
                goto error;
#else
            notdata = size;
#endif /* SEEK_HOLE */

            blobsize = notdata - offset;
            if (blobsize > chunksize)
                blobsize = chunksize;
            if (blobvec_append (blobvec,
                                mapbuf,
                                offset,
                                blobsize,
                                hashtype) < 0)
                goto error;
            offset += blobsize;
        }
    }
    return blobvec;
error:
    ERRNO_SAFE_WRAP (json_decref, blobvec);
    return NULL;
}

static json_t *fileref_create_nonempty (const char *path,
                                        const char *encoding,
                                        json_t *data,
                                        struct stat *sb,
                                        flux_error_t *error)
{
    json_t *o;

    if (!(o = json_pack ("{s:s s:s s:O s:I s:I s:I s:i}",
                         "path", path,
                         "encoding", encoding,
                         "data", data,
                         "size", (json_int_t)sb->st_size,
                         "mtime", (json_int_t)sb->st_mtime,
                         "ctime", (json_int_t)sb->st_ctime,
                         "mode", sb->st_mode))) {
        errprintf (error, "%s: error packing %s file object", path, encoding);
        errno = ENOMEM;
        return NULL;
    }
    return o;
}

static json_t *fileref_create_blobvec (const char *path,
                                       int fd,
                                       void *mapbuf,
                                       struct stat *sb,
                                       const char *hashtype,
                                       size_t chunksize,
                                       flux_error_t *error)
{
    json_t *blobvec;
    json_t *o;

    blobvec = blobvec_create (fd, mapbuf, sb->st_size, hashtype, chunksize);
    if (!blobvec) {
        errprintf (error,
                   "%s: error creating blobvec array: %s",
                   path,
                   strerror (errno));
        goto error;
    }
    if (!(o = fileref_create_nonempty (path, "blobvec", blobvec, sb, error)))
        goto error;
    json_decref (blobvec);
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, blobvec);
    return NULL;
}

static void *read_whole_file (const char *path,
                              int fd,
                              size_t size,
                              flux_error_t *error)
{
    void *data;
    ssize_t n;

    if ((n = read_all (fd, &data)) < 0) {
        errprintf (error, "%s: %s", path, strerror (errno));
        return NULL;
    }
    if (n < size) {
        errprintf (error, "%s: short read", path);
        free (data);
        errno = EINVAL;
    }
    return data;
}

static json_t *fileref_create_base64 (const char *path,
                                      void *data,
                                      struct stat *sb,
                                      flux_error_t *error)
{
    json_t *o;
    char *buf = NULL;
    size_t bufsize;
    json_t *obuf = NULL;

    bufsize = base64_encoded_length (sb->st_size) + 1; // +1 NULL
    if (!(buf = malloc (bufsize))) {
        errprintf (error, "%s: out of memory while encoding", path);
        return NULL;
    }
    if (base64_encode (buf, bufsize, data, sb->st_size) < 0) {
        errprintf (error, "%s: base64_encode error", path);
        goto inval;
    }
    if (!(obuf = json_string (buf))) {
        errprintf (error, "%s: error creating base64 json string", path);
        errno = EINVAL;
        goto error;
    }
    if (!(o = fileref_create_nonempty (path, "base64", obuf, sb, error)))
        goto error;
    json_decref (obuf);
    free (buf);
    return o;
inval:
    errno = EINVAL;
error:
    ERRNO_SAFE_WRAP (json_decref , obuf);
    ERRNO_SAFE_WRAP (free, buf);
    return NULL;
}

static json_t *fileref_create_utf8 (const char *path,
                                    void *data,
                                    struct stat *sb,
                                    flux_error_t *error)
{
    json_t *o;
    json_t *obuf = NULL;

    if (!(obuf = json_stringn (data, sb->st_size))) {
        errprintf (error, "%s: error creating utf-8 json string", path);
        errno = EINVAL;
        goto error;
    }
    if (!(o = fileref_create_nonempty (path, "utf-8", obuf, sb, error)))
        goto error;
    json_decref (obuf);
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref , obuf);
    return NULL;
}

static json_t *fileref_create_empty (const char *path,
                                     struct stat *sb,
                                     flux_error_t *error)
{
    json_t *o;

    if (!(o = json_pack ("{s:s s:I s:I s:I s:i}",
                         "path", path,
                         "size", (json_int_t)sb->st_size,
                         "mtime", (json_int_t)sb->st_mtime,
                         "ctime", (json_int_t)sb->st_ctime,
                         "mode", sb->st_mode))) {
        errprintf (error, "%s: error packing empty file object", path);
        errno = ENOMEM;
        return NULL;
    }
    return o;
}

static json_t *fileref_create_directory (const char *path,
                                         struct stat *sb,
                                         flux_error_t *error)
{
    json_t *o;

    if (!(o = json_pack ("{s:s s:I s:I s:i}",
                         "path", path,
                         "mtime", (json_int_t)sb->st_mtime,
                         "ctime", (json_int_t)sb->st_ctime,
                         "mode", sb->st_mode))) {
        errprintf (error, "%s: error packing directory file object", path);
        errno = ENOMEM;
        return NULL;
    }
    return o;
}

static json_t *fileref_create_symlink (const char *path,
                                       const char *fullpath,
                                       struct stat *sb,
                                       flux_error_t *error)
{
    json_t *o;
    char *target;

    if (!(target = calloc (1, sb->st_size + 1))
        || readlink (fullpath, target, sb->st_size) < 0) {
        errprintf (error, "readlink %s: %s", fullpath, strerror (errno));
        goto error;
    }
    if (!(o = json_pack ("{s:s s:s s:I s:I s:i}",
                         "path", path,
                         "data", target,
                         "mtime", (json_int_t)sb->st_mtime,
                         "ctime", (json_int_t)sb->st_ctime,
                         "mode", sb->st_mode))) {
        errprintf (error, "%s: error packing symlink file object", path);
        errno = ENOMEM;
        goto error;
    }
    free (target);
    return o;
error:
    ERRNO_SAFE_WRAP (free, target);
    return NULL;
}

json_t *fileref_create_ex (const char *path,
                           struct blobvec_param *param,
                           struct blobvec_mapinfo *mapinfop,
                           flux_error_t *error)
{
    const char *relative_path;
    json_t *o;
    int fd = -1;
    struct stat sb;
    struct blobvec_mapinfo mapinfo = { .base = MAP_FAILED, .size = 0 };
    int saved_errno;

    if (param) {
        if (param->hashtype == NULL) {
            errprintf (error, "invalid blobvec encoding parameters");
            goto inval;
        }
    }
    /* Store a relative path in the object so that extraction can specify a
     * destination directory, like tar(1) default behavior.
     */
    relative_path = path;
    while (*relative_path == '/')
        relative_path++;
    if (strlen (relative_path) == 0)
        relative_path = ".";
    /* Avoid TOCTOU in S_ISREG case by opening before checking its type.
     * If open fails due to O_NOFOLLOW (ELOOP), get link info with lstat(2).
     * Avoid open(2) blocking on a FIFO with O_NONBLOCK, but restore blocking
     * behavior after open(2) succeeds.
     */
    if ((fd = open (path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK)) < 0) {
        if (errno != ELOOP || lstat (path, &sb) < 0) {
            errprintf (error, "%s: %s", path, strerror (errno));
            goto error;
        }
    }
    else {
        if (fstat (fd, &sb) < 0 || fd_set_blocking (fd) < 0) {
            errprintf (error, "%s: %s", path, strerror (errno));
            goto error;
        }
    }

    /* Empty reg file, possibly sparse with size > 0.
     */
    if (S_ISREG (sb.st_mode) && file_has_no_data (fd)) {
        if (!(o = fileref_create_empty (relative_path, &sb, error)))
            goto error;
    }
    /* Large reg file will be encoded with blobvec.
     */
    else if (S_ISREG (sb.st_mode)
        && param != NULL
        && sb.st_size > param->small_file_threshold) {
        size_t chunksize;

        mapinfo.size = sb.st_size;
        mapinfo.base = mmap (NULL, mapinfo.size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapinfo.base == MAP_FAILED) {
            errprintf (error, "mmap: %s", strerror (errno));
            goto error;
        }
        chunksize = param->chunksize;
        if (chunksize == 0)
            chunksize = sb.st_size;
        if (!(o = fileref_create_blobvec (relative_path,
                                          fd,
                                          mapinfo.base,
                                          &sb,
                                          param->hashtype,
                                          chunksize,
                                          error)))
            goto error;
    }
    /* Other reg file will be encoded with base64.
     */
    else if (S_ISREG (sb.st_mode)) {
        void *data;
        if (!(data = read_whole_file (path, fd, sb.st_size, error)))
            goto error;
        if (!(o = fileref_create_utf8 (relative_path, data, &sb, error))
            && !(o = fileref_create_base64 (relative_path, data, &sb, error))) {
            ERRNO_SAFE_WRAP (free, data);
            goto error;
        }
        free (data);
    }
    /* symlink
     */
    else if (S_ISLNK (sb.st_mode)) {
        if (!(o = fileref_create_symlink (relative_path, path, &sb, error)))
            goto error;
    }
    /* directory
     */
    else if (S_ISDIR (sb.st_mode)) {
        if (!(o = fileref_create_directory (relative_path, &sb, error)))
            goto error;
    }
    else {
        errprintf (error, "%s: unsupported file type", path);
        goto inval;
    }

    if (mapinfop)
        *mapinfop = mapinfo;
    else
        (void)munmap (mapinfo.base, mapinfo.size);
    if (fd >= 0)
        close (fd);
    return o;
inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    if (mapinfo.base != MAP_FAILED)
        (void)munmap (mapinfo.base, mapinfo.size);
    if (fd >= 0)
        close (fd);
    errno = saved_errno;
    return NULL;
}

json_t *fileref_create (const char *path, flux_error_t *error)
{
    return fileref_create_ex (path, NULL, NULL, error);
}

void fileref_pretty_print (json_t *fileref,
                           const char *path,
                           bool long_form,
                           char *buf,
                           size_t bufsize)
{
    json_int_t size = 0;
    int mode;
    int n;

    if (!buf)
        return;
    /* RFC 37 says path is optional in the file object (to support dict archive
     * containers) so let it be passed in as 'path' arg and override if present
     * in the object.  It's an error if it's not set by one of those.
     */
    if (!fileref
        || json_unpack (fileref,
                        "{s?s s:i s?I}",
                        "path", &path,
                        "mode", &mode,
                        "size", &size) < 0
        || path == NULL) {
        n = snprintf (buf, bufsize, "invalid fileref");
    }
    else if (long_form) {
        n = snprintf (buf,
                      bufsize,
                      "%s 0%o %8ju %s",
                      S_ISREG (mode) ? "f" :
                      S_ISLNK (mode) ? "l" :
                      S_ISDIR (mode) ? "d" : "?",
                      mode & 0777,
                      (uintmax_t)size,
                      path);
    }
    else
        n = snprintf (buf, bufsize, "%s", path);
    if (n >= bufsize && bufsize > 1)
        buf[bufsize - 2] = '+';
}

// vi:tabstop=4 shiftwidth=4 expandtab
