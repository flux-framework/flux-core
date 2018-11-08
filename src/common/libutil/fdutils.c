/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
