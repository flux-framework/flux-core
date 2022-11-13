/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* fileref.c - manipulate "fileref" object
 *
 * { "version":1,
 *   "type":"fileref",
 *   "path":s,
 *   "size":I,
 *   "ctime":I,
 *   "mtime":I,
 *   "mode":i,
 *   "data"?s,
 *   "blobvec":[
 *      [I,I,s],
 *      [I,I,s],
 *      ...
 *   ]
 * }
 *
 * where blobvec array entries are a 3-tuple of [offset, size, blobref]
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

#include "blobref.h"
#include "errno_safe.h"
#include "errprintf.h"
#include "read_all.h"
#include "fileref.h"

static const int small_file_threshold = 4096;

static int blobvec_append (json_t *blobvec,
                           const void *mapbuf,
                           off_t offset,
                           size_t blobsize,
                           const char *hashtype)
{
    char blobref[BLOBREF_MAX_STRING_SIZE];
    json_t *o;

    if (blobref_hash (hashtype,
                      mapbuf + offset,
                      blobsize,
                      blobref,
                      sizeof (blobref)) < 0)
        return -1;
    if (!(o = json_pack ("[I,I,s]", offset, blobsize, blobref))
        || json_array_append_new (blobvec, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Walk the regular file represented by 'fd', appending blobvec array entries
 * to 'blobvec' array for each 'chunksize' region.  Use SEEK_DATA and SEEK_HOLE
 * to skip holes in sparse files - see lseek(2).
 */
static int blobvec_populate (json_t *blobvec,
                             int fd,
                             const void *mapbuf,
                             size_t size,
                             const char *hashtype,
                             int chunksize)
{
    off_t offset = 0;

    assert (fd >= 0);
    assert (size > 0);

    while (offset < size) {
        // N.B. fails with ENXIO if there is no more data
        if ((offset = lseek (fd, offset, SEEK_DATA)) == (off_t)-1) {
            if (errno == ENXIO)
                break;
            return -1;
        }
        if (offset < size) {
            off_t notdata;
            int blobsize;

            // N.B. returns size if there are no more holes
            if ((notdata = lseek (fd, offset, SEEK_HOLE)) == (off_t)-1)
                return -1;
            blobsize = notdata - offset;
            if (blobsize > chunksize)
                blobsize = chunksize;
            if (blobvec_append (blobvec,
                                mapbuf,
                                offset,
                                blobsize,
                                hashtype) < 0)
                return -1;
            offset += blobsize;
        }
    }
    return 0;
}

/* Optionally set fileref.data (string).
 * If 'data' is NULL do nothing.
 * If encode_base64 is false, assume 'data' is NULL terminated and use directly.
 * If encode_base64 is true, base64-encode it first.
 */
static int set_optional_data (json_t *fileref,
                              const void *data,
                              size_t data_size,
                              bool encode_base64)
{
    if (data) {
        json_t *o;

        if (encode_base64) {
            char *buf = NULL;
            size_t bufsize = base64_encoded_length (data_size) + 1; // +1 NULL

            if (!(buf = malloc (bufsize)))
                return -1;
            if (base64_encode (buf, bufsize, data, data_size) < 0) {
                free (buf);
                errno = EINVAL;
                return -1;
            }
            o = json_string (buf);
            free (buf);
        }
        else
            o = json_string (data);
        if (!o
            || json_object_set_new (fileref, "data", o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

/* Create fileref object representing regular file, symbolic link, or
 * directory. 'fullpath' is used to open/stat the file, while 'path' is
 * recorded in the fileref object.
 */
json_t *fileref_create_ex (const char *path,
                           const char *fullpath,
                           const char *hashtype,
                           int chunksize,
                           void **mapbufp,
                           size_t *mapsizep,
                           flux_error_t *error)
{
    json_t *blobvec = NULL;
    json_t *o;
    int fd = -1;
    struct stat sb;
    void *mapbuf = MAP_FAILED;
    size_t mapsize = 0;
    int saved_errno;
    void *data = NULL;
    ssize_t data_size = 0;
    bool data_encode_base64 = false;
    int rc;

    if (chunksize < 0) {
        errprintf (error, "chunksize cannot be negative");
        goto inval;
    }
    if (!fullpath)
        fullpath = path;
    /* Avoid TOCTOU in S_ISREG case by opening before checking its type.
     */
    if ((fd = open (fullpath, O_RDONLY | O_NOFOLLOW)) < 0)
        rc = lstat (fullpath, &sb);
    else
        rc = fstat (fd, &sb);
    if (rc < 0) {
        errprintf (error, "%s: %s", path, strerror (errno));
        goto error;
    }
    if (!(blobvec = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    /* Regular file: if size is below threshold, base64 encode its content
     * and place in the 'data' field.  Otherwise, mmap the file and build
     * the blobvec array.
     */
    if (S_ISREG (sb.st_mode)) {
        if (sb.st_size > 0) {
            if (sb.st_size <= small_file_threshold) {
                if ((data_size = read_all (fd, &data)) < 0) {
                    errprintf (error, "%s: %s", path, strerror (errno));
                    goto error;
                }
                if (data_size < sb.st_size) {
                    errprintf (error, "%s: short read", path);
                    errno = EINVAL;
                    goto error;
                }
                data_encode_base64 = true;
            }
            else {
                mapsize = sb.st_size;
                mapbuf = mmap (NULL, mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
                if (mapbuf == MAP_FAILED) {
                    errprintf (error, "mmap: %s", strerror (errno));
                    goto error;
                }
                if (chunksize == 0)
                    chunksize = sb.st_size;
                if (blobvec_populate (blobvec,
                                      fd,
                                      mapbuf,
                                      mapsize,
                                      hashtype,
                                      chunksize) < 0) {
                    errprintf (error,
                               "error generating blobvec array: %s",
                               strerror (errno));
                    goto error;
                }
            }
        }
    }
    /* Symbolic link: place link target in 'data' field.
     */
    else if (S_ISLNK (sb.st_mode)) {
        if (!(data = calloc (1, sb.st_size + 1))
            || readlink (fullpath, data, sb.st_size) < 0) {
            errprintf (error, "readlink %s: %s", path, strerror (errno));
            goto error;
        }
        data_size = strlen (data);
    }
    /* For a directory, 'data' is not set, and 'blobvec' remains empty.
     * All other types are rejected.
     */
    else if (!S_ISDIR (sb.st_mode)) {
        errprintf (error, "%s: unsupported file type", path);
        goto inval;
    }
    /* Store a relative path in the object so that extraction can specify a
     * destination directory, like tar(1) default behavior.
     */
    const char *relative_path = path;
    while (*relative_path == '/')
        relative_path++;
    if (strlen (relative_path) == 0)
        relative_path = ".";
    json_int_t size = sb.st_size;
    json_int_t ctime = sb.st_ctime;
    json_int_t mtime = sb.st_mtime;
    int mode = sb.st_mode;
    if (!(o = json_pack ("{s:i s:s s:s s:I s:I s:I s:i s:O}",
                         "version", 1,
                         "type", "fileref",
                         "path", relative_path,
                         "size", size,
                         "mtime", mtime,
                         "ctime", ctime,
                         "mode", mode,
                         "blobvec", blobvec))
        || set_optional_data (o, data, data_size, data_encode_base64) < 0) {
        errprintf (error, "error packing fileref object");
        goto inval;
    }
    if (mapbufp)
        *mapbufp = mapbuf;
    else if (mapbuf != MAP_FAILED)
        (void)munmap (mapbuf, mapsize);
    if (mapsizep)
        *mapsizep = mapsize;
    if (fd >= 0)
        close (fd);
    json_decref (blobvec);
    free (data);
    return o;
inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    json_decref (blobvec);
    free (data);
    if (mapbuf != MAP_FAILED)
        (void)munmap (mapbuf, mapsize);
    if (fd >= 0)
        close (fd);
    errno = saved_errno;
    return NULL;
}

json_t *fileref_create (const char *path,
                        const char *hashtype,
                        int chunksize,
                        flux_error_t *error)
{
    return fileref_create_ex (path,
                              NULL, // fullpath
                              hashtype,
                              chunksize,
                              NULL, // mapbuf
                              NULL, // maplen
                              error);
}

void fileref_pretty_print (json_t *fileref,
                           bool long_form,
                           char *buf,
                           size_t bufsize)
{
    const char *path;
    json_int_t size;
    json_int_t ctime;
    json_int_t mtime;
    int mode;
    json_t *blobvec;
    const char *data = NULL;
    int n;

    if (!buf)
        return;
    if (!fileref
        || json_unpack (fileref,
                        "{s:s s:I s:I s:I s:i s?s s:o}",
                        "path", &path,
                        "size", &size,
                        "mtime", &mtime,
                        "ctime", &ctime,
                        "mode", &mode,
                        "data", &data,
                        "blobvec", &blobvec) < 0) {
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
