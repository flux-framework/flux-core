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
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "dgetline.h"

int dgetline (int fd, char *buf, int len)
{
    int i = 0;
    while (i < len - 1) {
        if (read (fd, &buf[i], 1) <= 0)
            return -1;
        if (buf[i++] == '\n')
            break;
    }
    if (buf[i - 1] != '\n') {
        errno = EPROTO;
        return -1;
    }
    buf[i] = '\0';
    return 0;
}

int dputline (int fd, const char *buf)
{
    int len = strlen (buf);
    int n, count = 0;
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
