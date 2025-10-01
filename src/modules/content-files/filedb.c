/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "filedb.h"


static int filedb_input_check (const char *key, const char **errstr)
{
    if (strlen (key) == 0 || strchr (key, '/') || streq (key, "..")
                          || streq (key, ".")) {
        errno = EINVAL;
        if (errstr)
            *errstr = "invalid key name";
        return -1;
    }
    return 0;
}

static int filedb_path (const char *dbpath,
                        const char *key,
                        char *path,
                        size_t path_len,
                        const char **errstr)
{
    if (snprintf (path, path_len, "%s/%s", dbpath, key) >= path_len) {
        errno = EOVERFLOW;
        if (errstr)
            *errstr = "key name too long for internal buffer";
        return -1;
    }
    return 0;
}

int filedb_get (const char *dbpath,
                const char *key,
                void **datap,
                size_t *sizep,
                const char **errstr)
{
    char path[1024];
    int fd;
    void *data;
    ssize_t size;

    if (filedb_input_check (key, errstr) < 0
        || filedb_path (dbpath, key, path, sizeof (path), errstr) < 0)
        return -1;
    if ((fd = open (path, O_RDONLY)) < 0)
        return -1;
    if ((size = read_all (fd, &data)) < 0) {
        ERRNO_SAFE_WRAP (close, fd);
        return -1;
    }
    if (close (fd) < 0) {
        ERRNO_SAFE_WRAP (free, data);
        return -1;
    }
    *datap = data;
    *sizep = size;
    return 0;
}

int filedb_put (const char *dbpath,
                const char *key,
                const void *data,
                size_t size,
                const char **errstr)
{
    char path[1024];
    int fd;

    if (filedb_input_check (key, errstr) < 0
        || filedb_path (dbpath, key, path, sizeof (path), errstr) < 0)
        return -1;
    if ((fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        return -1;
    if (write_all (fd, data, size) < 0) {
        ERRNO_SAFE_WRAP (close, fd);
        return -1;
    }
    if (close (fd) < 0)
        return -1;
    return 0;
}

int filedb_validate (const char *dbpath,
                     const char *key,
                     const char **errstr)
{
    char path[1024];
    struct stat statbuf;

    if (filedb_input_check (key, errstr) < 0
        || filedb_path (dbpath, key, path, sizeof (path), errstr) < 0)
        return -1;
    if (stat (path, &statbuf) < 0)
        return -1;
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
