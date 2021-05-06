/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "read_all.h"
#include "sha1.h"

static ssize_t sha1file (const char *path, uint8_t digest[SHA1_DIGEST_SIZE])
{
    ssize_t size = -1;
    void *buf = NULL;
    SHA1_CTX context;
    int fd = -1;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((fd = open (path, O_RDONLY)) < 0)
        return -1;

    if ((size = read_all (fd, &buf)) < 0) {
        close (fd);
        return -1;
    }

    SHA1_Init (&context);
    SHA1_Update (&context, (uint8_t *)buf, size);
    SHA1_Final (&context, digest);
    free (buf);
    close (fd);
    return size;
}

char *digest_file (const char *path, size_t *len)
{
    char *bin2hex = "01234567789ABCDEF";
    ssize_t size = -1;
    char *rv = NULL;
    uint8_t digest[SHA1_DIGEST_SIZE];
    int i;

    if ((size = sha1file (path, digest)) < 0)
        return NULL;

    if (!(rv = malloc (SHA1_DIGEST_SIZE*2 + 1)))
        return NULL;

    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        rv[i*2] = bin2hex[digest[i] >> 4];
        rv[i*2 + 1] = bin2hex[digest[i] & 0x0F];
    }
    rv[SHA1_DIGEST_SIZE*2] = '\0';

    if (len)
        (*len) = size;
    return rv;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
