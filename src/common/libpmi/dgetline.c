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
