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
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "ipaddr.h"

int ipaddr_getprimary (char *buf, int len, char *errstr, int errstrsz)
{
    char hostname[HOST_NAME_MAX + 1];
    struct addrinfo hints, *res = NULL;
    int e;

    if (gethostname (hostname, sizeof (hostname)) < 0) {
        if (errstr)
            snprintf (errstr, errstrsz, "gethostname: %s", strerror (errno));
        return -1;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((e = getaddrinfo (hostname, NULL, &hints, &res)) || res == NULL) {
        if (errstr)
            snprintf (errstr, errstrsz, "getaddrinfo %s: %s",
                      hostname, gai_strerror (e));
        return -1;
    }
    if ((e = getnameinfo (res->ai_addr, res->ai_addrlen, buf, len,
                          NULL, 0, NI_NUMERICHOST))) {
        if (errstr)
            snprintf (errstr, errstrsz, "getnameinfo: %s", gai_strerror (e));
        freeaddrinfo (res);
        return -1;
    }
    freeaddrinfo (res);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
