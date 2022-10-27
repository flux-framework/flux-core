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
#include "config.h"
#endif
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "log.h"
#include "ipaddr.h"

static __attribute__ ((format (printf, 3, 4)))
void esprintf (char *buf, int len, const char *fmt, ...)
{
    if (buf) {
        va_list ap;

        va_start (ap, fmt);
        vsnprintf (buf, len, fmt, ap);
        va_end (ap);
    }
}

/* Identify an IPv6 link-local address so it can be skipped.
 * The leftmost 10 bits of the 128 bit address will be 0xfe80.
 * At present, Flux cannot use link-local addresses for PMI bootstrap,
 * as the scope (e.g. %index or %iface-name) is not valid off the local node.
 * See also flux-framework/flux-core#3378
 */
static bool is_linklocal6 (struct sockaddr_in6 *sin)
{
    if (sin->sin6_addr.s6_addr[0] == 0xfe
            && (sin->sin6_addr.s6_addr[1] & 0xc0) == 0x80)
        return true;
    return false;
}

static int getprimary_iface4 (char *buf, size_t size,
                              char *errstr, int errstrsz)
{
    const char *path = "/proc/net/route";
    FILE *f;
    unsigned long dest;
    char line[256];

    if (!(f = fopen (path, "r"))) {
        esprintf (errstr, errstrsz, "%s: %s", path, strerror (errno));
        return -1;
    }
    while (fgets (line, sizeof (line), f)) {
        char fmt[24];
        /*  Format guaranteed to fit in 24 bytes (assuming 10 digit max) */
        (void) snprintf (fmt,
                         sizeof(fmt),
                         "%%%us\t%%lx",
                         (unsigned) size - 1);
        if (sscanf (line, fmt, buf, &dest) == 2 && dest == 0) {
            fclose (f);
            return 0;
        }
    }
    fclose (f);
    esprintf (errstr, errstrsz, "%s: no default route", path);
    return -1;
}

static struct ifaddrs *find_ifaddr (struct ifaddrs *ifaddr,
                                    const char *name,
                                    int family)
{
    struct ifaddrs *ifa;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!strcmp (ifa->ifa_name, name)
                && ifa->ifa_addr != NULL
                && ifa->ifa_addr->sa_family == family
                && (ifa->ifa_addr->sa_family != AF_INET6
                    || !is_linklocal6 ((struct sockaddr_in6 *)ifa->ifa_addr)))
            break;
    }
    return ifa;
}

static int getprimary_ifaddr (char *buf, int len, int prefer_family,
                              char *errstr, int errstrsz)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    char name[64];
    int error;

    if (getprimary_iface4 (name, sizeof (name), errstr, errstrsz) < 0)
        return -1;
    if (getifaddrs (&ifaddr) < 0) {
        esprintf (errstr, errstrsz, "getifaddrs: %s", strerror (errno));
        return -1;
    }
    if (!(ifa = find_ifaddr (ifaddr, name, prefer_family))) {
        prefer_family = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;
        ifa = find_ifaddr (ifaddr, name, prefer_family);
    }
    if (!ifa) {
        esprintf (errstr, errstrsz, "could not find address of %s", name);
        freeifaddrs (ifaddr);
        return -1;
    }
    error = getnameinfo (ifa->ifa_addr,
                         ifa->ifa_addr->sa_family == AF_INET
                             ? sizeof (struct sockaddr_in)
                             : sizeof (struct sockaddr_in6),
                         buf, // <== result copied here
                         len,
                         NULL,
                         0,
                         NI_NUMERICHOST);
    if (error) {
        esprintf (errstr, errstrsz, "getnameinfo: %s", gai_strerror (error));
        freeifaddrs (ifaddr);
        return -1;
    }
    freeifaddrs (ifaddr);
    return 0;
}

static struct addrinfo *find_addrinfo (struct addrinfo *addrinfo, int family)
{
    struct addrinfo *ai;

    for (ai = addrinfo; ai != NULL; ai = ai->ai_next) {
        if (ai->ai_family == family
            && (ai->ai_family != AF_INET6
                || !is_linklocal6 ((struct sockaddr_in6 *)ai->ai_addr)))
            break;
    }
    return ai;
}

static int getprimary_hostaddr (char *buf, int len, int prefer_family,
                                char *errstr, int errstrsz)
{
    char hostname[HOST_NAME_MAX + 1];
    struct addrinfo hints, *res = NULL;
    struct addrinfo *rp;
    int error;

    if (gethostname (hostname, sizeof (hostname)) < 0) {
        esprintf (errstr, errstrsz, "gethostname: %s", strerror (errno));
        return -1;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    error = getaddrinfo (hostname, NULL, &hints, &res);
    if (error) {
        esprintf (errstr, errstrsz, "getaddrinfo %s: %s",
                  hostname, gai_strerror (error));
        return -1;
    }
    if (!(rp = find_addrinfo (res, prefer_family))) {
        prefer_family = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;
        rp = find_addrinfo (res, prefer_family);
    }
    if (!rp) {
        esprintf (errstr, errstrsz, "could not find address of %s", hostname);
        freeaddrinfo (res);
        return -1;
    }
    error = getnameinfo (rp->ai_addr,
                         rp->ai_addrlen,
                         buf, // <== result copied here
                         len,
                         NULL,
                         0,
                         NI_NUMERICHOST);
    if (error) {
        esprintf (errstr, errstrsz, "getnameinfo: %s", gai_strerror (error));
        freeaddrinfo (res);
        return -1;
    }
    freeaddrinfo (res);
    return 0;
}

int ipaddr_getprimary (char *buf, int len,
                       char *errstr, int errstrsz)
{
    int prefer_family = getenv ("FLUX_IPADDR_V6") ? AF_INET6 : AF_INET;
    int rc = -1;

    if (getenv ("FLUX_IPADDR_HOSTNAME") == NULL)
        rc = getprimary_ifaddr (buf, len, prefer_family, errstr, errstrsz);
    if (rc < 0)
        rc = getprimary_hostaddr (buf, len, prefer_family, errstr, errstrsz);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
