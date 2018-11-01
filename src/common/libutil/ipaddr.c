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
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <argz.h>

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

int ipaddr_getall (char **addrs, size_t *addrssz, char *errstr, int errstrsz)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;
    int family;
    int e;
    char host[NI_MAXHOST];
    int rc = -1;

    if (getifaddrs (&ifaddr) < 0) {
        if (errstr)
            snprintf (errstr, errstrsz, "getifaddrs: %s", strerror (errno));
        return -1;
    }
    /* Ref: getifaddrs(3) example */
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;
        e = getnameinfo (ifa->ifa_addr,
                         family == AF_INET ? sizeof (struct sockaddr_in)
                                           : sizeof (struct sockaddr_in6),
                         host, NI_MAXHOST,
                         NULL, 0, NI_NUMERICHOST);
        if (e != 0) {
            if (errstr)
                snprintf (errstr, errstrsz, "getnameinfo: %s",
                          gai_strerror (e));
            goto done;
        }
        if ((e = argz_add (addrs, addrssz, host)) != 0) {
            if (errstr)
                snprintf (errstr, errstrsz, "argz_add: %s", strerror (errno));
            goto done;
        }
    }
    rc = 0;
done:
    if (ifaddr)
        freeifaddrs (ifaddr);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
