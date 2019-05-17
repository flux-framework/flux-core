/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "read_all.h"

ssize_t write_all (int fd, const void *buf, size_t len)
{
    ssize_t n;
    ssize_t count = 0;

    if (fd < 0 || (buf == NULL && len != 0)) {
        errno = EINVAL;
        return -1;
    }
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return -1;
        count += n;
    }
    return count;
}

ssize_t read_all (int fd, void **bufp)
{
    const size_t chunksize = 4096;
    size_t len = 0;
    void *buf = NULL;
    ssize_t n;
    ssize_t count = 0;
    void *new;
    int saved_errno;

    if (fd < 0 || !bufp) {
        errno = EINVAL;
        return -1;
    }
    do {
        if (len - count == 0) {
            len += chunksize;
            if (!(new = realloc (buf, len + 1)))
                goto error;
            buf = new;
        }
        if ((n = read (fd, buf + count, len - count)) < 0)
            goto error;
        count += n;
    } while (n != 0);
    ((char *)buf)[count] = '\0';
    *bufp = buf;
    return count;
error:
    saved_errno = errno;
    free (buf);
    errno = saved_errno;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
