/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell std output file writer and hash abstraction
 *
 * Handle opening of unique file paths for std output using
 * shell output file options. If a file is already open (i.e.
 * if more than one task is writing to the same path), then
 * increment a refcount and return the same file entry.
 *
 * Files are closed when refcounts go to zero or the file hash
 * is destroyed.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/read_all.h"

#include "output/filehash.h"

struct filehash {
    zhashx_t *files;
};

static void file_entry_destroy (struct file_entry *fp)
{
    if (fp) {
        int saved_errno = errno;
        if (fp->fd >= 0)
            close (fp->fd);
        free (fp->path);
        free (fp);
        errno = saved_errno;
    }
}

static void file_entry_destructor (void **item)
{
    if (item) {
        file_entry_destroy (*item);
        *item = NULL;
    }
}

int file_entry_write (struct file_entry *fp,
                      const char *label,
                      const char *data,
                      int len)
{
    if (len > 0) {
        if (fp->label && dprintf (fp->fd, "%s: ", label) < 0)
            return -1;
        return write_all (fp->fd, data, len);
    }
    return 0;
}

static struct file_entry *file_entry_open (const char *path,
                                           int flags,
                                           bool label,
                                           flux_error_t *errp)
{
    struct file_entry *fp;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

   if (!(fp = calloc (1, sizeof (*fp)))) {
        errprintf (errp, "Out of memory");
        goto error;
    }
    if ((fp->fd = open (path, flags, mode)) < 0) {
        errprintf (errp,
                   "error opening output file '%s': %s",
                   path,
                   strerror (errno));
        goto error;
    }
    if (!(fp->path = strdup (path))) {
        errprintf (errp, "Out of memory");
        goto error;
    }
    fp->label = label;
    fp->refcount = 1;
    return fp;
error:
    file_entry_destroy (fp);
    return NULL;
}

void file_entry_close (struct file_entry *fp)
{
    if (fp && --fp->refcount == 0)
        zhashx_delete (fp->fh->files, fp->path);
}

struct file_entry *filehash_open (struct filehash *fh,
                                  flux_error_t *errp,
                                  const char *path,
                                  int flags,
                                  bool label)
{
    struct file_entry *fp;

    if ((fp = zhashx_lookup (fh->files, path)))
        return filehash_entry_incref (fp);

    if (!(fp = file_entry_open (path, flags, label, errp))) {
        errprintf (errp, "open: %s: %s", path, strerror (errno));
        return NULL;
    }
    (void) zhashx_insert (fh->files, path, fp);
    fp->fh = fh;
    return fp;
}

struct file_entry *filehash_entry_incref (struct file_entry *fp)
{
    if (fp)
        fp->refcount++;
    return fp;
}

void filehash_destroy (struct filehash *fh)
{
    if (fh) {
        int saved_errno = errno;
        zhashx_destroy (&fh->files);
        free (fh);
        errno = saved_errno;
    }
}

struct filehash *filehash_create ()
{
    struct filehash *fh;

    if (!(fh = calloc (1, sizeof (*fh)))
        || !(fh->files = zhashx_new ()))
        goto error;
    zhashx_set_destructor (fh->files, file_entry_destructor);
    return fh;
error:
    filehash_destroy (fh);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
