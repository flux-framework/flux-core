/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
            if (!(new = realloc (buf, len+1)))
                goto error;
            buf = new;
        }
        if ((n = read (fd, buf + count, len - count)) < 0)
            goto error;
        count += n;
    } while (n != 0);
    ((char*)buf)[count] = '\0';
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
