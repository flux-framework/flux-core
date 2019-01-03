/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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

#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#include "fdutils.h"

int fd_get_flags (int fd)
{
    return fcntl (fd, F_GETFL);
}

int fd_set_flags (int fd, int flags)
{
    return fcntl (fd, F_SETFL, flags);
}

static int fd_setfl (int fd, int flag, bool set)
{
    int flags = fd_get_flags (fd);
    if ((flags < 0)
       || (fd_set_flags (fd, set ? flags|flag : flags & ~flag) < 0))
        return -1;
    return (flags);
}

static int fd_setfd (int fd, int flag, bool set)
{
    int flags = fcntl (fd, F_GETFD);
    if ((flags < 0)
       || (fcntl (fd, F_SETFD, (set ? flags|flag : flags&~flag)) < 0))
        return -1;
    return (flags);
}

int fd_set_blocking (int fd)
{
    return (fd_setfl (fd, O_NONBLOCK, false));
}

int fd_set_nonblocking (int fd)
{
    return (fd_setfl (fd, O_NONBLOCK, true));
}

int fd_set_cloexec (int fd)
{
    return (fd_setfd (fd, FD_CLOEXEC, true));
}

int fd_unset_cloexec (int fd)
{
    return (fd_setfd (fd, FD_CLOEXEC, false));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
