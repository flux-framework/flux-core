/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <zmq.h>
#include <czmq.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <assert.h>

#include "log.h"
#include "xzmalloc.h"

#include "zfd.h"

static int _nonblock (int fd, bool nonblock)
{
    int flags;

    if ((flags = fcntl (fd, F_GETFL)) < 0)
        return -1;
    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl (fd, F_SETFL, flags) < 0)
        return -1;
    return 0;
}

static int _read_all (int fd, uint8_t *buf, size_t len, bool nonblock)
{
    int n, count = 0;

    do {
        if (nonblock && _nonblock (fd, true) < 0)
            return -1;
        n = read (fd, buf + count, len - count);
        if (nonblock && _nonblock (fd, false) < 0)
            return -1;
        nonblock = false;
        if (n <= 0)
            return n;
        count += n;
    } while (count < len);

    return count;
}

static int _write_all (int fd, uint8_t *buf, size_t len)
{
    int n, count = 0;

    do {
        n = write (fd, buf + count, len - count);
        if (n < 0)
            return n;
        count += n;
    } while (count < len);

    return count;
}

zmsg_t *zfd_recv_typemask (int fd, int *typemask, bool nonblock)
{
    uint8_t *buf = NULL;
    uint32_t len, mask;
    int n;
    zmsg_t *msg;

    if (typemask) {
        n = _read_all (fd, (uint8_t *)&mask, sizeof (mask), nonblock);
        if (n < 0)
            goto error;
        if (n == 0)
            goto eproto;
        mask = ntohl (mask);
    }

    n = _read_all (fd, (uint8_t *)&len, sizeof (len), 0);
    if (n < 0)
        goto error;
    if (n == 0)
        goto eproto;
    len = ntohl (len);

    buf = xzmalloc (len);
    n = _read_all (fd, buf, len, 0);
    if (n < 0)
        goto error;
    if (n == 0)
        goto eproto;

    msg = zmsg_decode ((byte *)buf, len);
    free (buf);
    if (typemask)
        *typemask = mask;
    return msg;
eproto:
    errno = EPROTO;
error:
    if (buf)
        free (buf);
    return NULL;
}

zmsg_t *zfd_recv (int fd, bool nonblock)
{
    return zfd_recv_typemask (fd, NULL, nonblock);
}

int zfd_send_typemask (int fd, int typemask, zmsg_t **msg)
{
    uint8_t *buf = NULL;
    int n, len;
    uint32_t nlen, mask;

    len = zmsg_encode (*msg, &buf);
    if (len < 0) {
        errno = EPROTO;
        goto error;
    }

    if (typemask != -1) {
        mask = htonl ((uint32_t)typemask);
        n = _write_all (fd, (uint8_t *)&mask, sizeof (mask));
        if (n < 0)
            goto error;
    }

    nlen = htonl ((uint32_t)len);
    n = _write_all (fd, (uint8_t *)&nlen, sizeof (nlen));
    if (n < 0)
        goto error;

    n = _write_all (fd, buf, len);
    if (n < 0)
        goto error;

    free (buf);
    zmsg_destroy (msg);
    return 0;
error:
    if (buf)
        free (buf);
    return -1;
}

int zfd_send (int fd, zmsg_t **msg)
{
    return zfd_send_typemask (fd, -1, msg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

