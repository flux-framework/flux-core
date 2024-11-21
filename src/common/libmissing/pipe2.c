/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <fcntl.h>
#include <errno.h>

#include "pipe2.h"

static int setflags (int fd, int flags)
{
    int oflags;
    if ((oflags = fcntl (fd, F_GETFL)) < 0
        || fcntl (fd, F_SETFL, oflags | flags) < 0)
        return -1;
    return 0;
}

int pipe2 (int pipefd[2], int flags)
{
    int pfd[2];
    if (pipe (pfd) < 0)
        return -1;
    if (setflags (pfd[0], flags) < 0 || setflags (pfd[1], flags) < 0) {
        int saved_errno = errno;
        (void)close (pfd[0]);
        (void)close (pfd[1]);
        errno = saved_errno;
        return -1;
    }
    pipefd[0] = pfd[0];
    pipefd[1] = pfd[1];
    return 0;
}

// vi:ts=4 sw=4 expandtab
