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

#include "ccan/str/str.h"
#include "errprintf.h"

#include "log.h"
#include "cidr.h"
#include "ipaddr.h"

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

static int getprimary_iface4 (char *buf, size_t size, flux_error_t *error)
{
    const char *path = "/proc/net/route";
    FILE *f;
    unsigned long dest;
    char line[256];

    if (!(f = fopen (path, "r"))) {
        errprintf (error, "%s: %s", path, strerror (errno));
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
    errprintf (error, "%s: no default route", path);
    return -1;
}

static struct ifaddrs *find_ifaddr (struct ifaddrs *ifaddr,
                                    const char *name,
                                    int family)
{
    struct ifaddrs *ifa;
    struct cidr4 cidr;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (streq (ifa->ifa_name, name)
            && ifa->ifa_addr != NULL
            && ifa->ifa_addr->sa_family == family
            && (ifa->ifa_addr->sa_family != AF_INET6
                || !is_linklocal6 ((struct sockaddr_in6 *)ifa->ifa_addr)))
            break;
    }
    if (ifa)
        return ifa;

    /* We didn't find an exact interface match for 'name' above, so
     * try parsing 'name' as a CIDR and match the interface address.
     * Only ipv4 is supported at this point.
     */
    if (family == AF_INET6 || cidr_parse4 (&cidr, name) < 0)
        return NULL;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (cidr_match4 (&cidr, &sin->sin_addr))
            break;
    }

    return ifa;
}

static int getnamed_ifaddr (char *buf,
                            int len,
                            const char *name,
                            int prefer_family,
                            flux_error_t *error)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    int e;

    if (getifaddrs (&ifaddr) < 0) {
        errprintf (error, "getifaddrs: %s", strerror (errno));
        return -1;
    }
    if (!(ifa = find_ifaddr (ifaddr, name, prefer_family))) {
        prefer_family = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;
        ifa = find_ifaddr (ifaddr, name, prefer_family);
    }
    if (!ifa) {
        errprintf (error, "could not find address of %s", name);
        freeifaddrs (ifaddr);
        return -1;
    }
    e = getnameinfo (ifa->ifa_addr,
                     ifa->ifa_addr->sa_family == AF_INET
                         ? sizeof (struct sockaddr_in)
                         : sizeof (struct sockaddr_in6),
                     buf, // <== result copied here
                     len,
                     NULL,
                     0,
                     NI_NUMERICHOST);
    if (e) {
        errprintf (error, "getnameinfo: %s", gai_strerror (e));
        freeifaddrs (ifaddr);
        return -1;
    }
    freeifaddrs (ifaddr);
    return 0;
}

static int getprimary_ifaddr (char *buf,
                              int len,
                              int prefer_family,
                              flux_error_t *error)
{
    char name[64];
    if (getprimary_iface4 (name, sizeof (name), error) < 0)
        return -1;
    return getnamed_ifaddr (buf, len, name, prefer_family, error);
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

static int getprimary_hostaddr (char *buf,
                                int len,
                                int prefer_family,
                                flux_error_t *error)
{
    char hostname[HOST_NAME_MAX + 1];
    struct addrinfo hints, *res = NULL;
    struct addrinfo *rp;
    int e;

    if (gethostname (hostname, sizeof (hostname)) < 0) {
        errprintf (error, "gethostname: %s", strerror (errno));
        return -1;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    e = getaddrinfo (hostname, NULL, &hints, &res);
    if (e) {
        errprintf (error, "getaddrinfo %s: %s", hostname, gai_strerror (e));
        return -1;
    }
    if (!(rp = find_addrinfo (res, prefer_family))) {
        prefer_family = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;
        rp = find_addrinfo (res, prefer_family);
    }
    if (!rp) {
        errprintf (error, "could not find address of %s", hostname);
        freeaddrinfo (res);
        return -1;
    }
    e = getnameinfo (rp->ai_addr,
                     rp->ai_addrlen,
                     buf, // <== result copied here
                     len,
                     NULL,
                     0,
                     NI_NUMERICHOST);
    if (e) {
        errprintf (error, "getnameinfo: %s", gai_strerror (e));
        freeaddrinfo (res);
        return -1;
    }
    freeaddrinfo (res);
    return 0;
}

int ipaddr_getprimary (char *buf,
                       int len,
                       int flags,
                       const char *interface,
                       flux_error_t *error)
{
    int prefer_family = (flags & IPADDR_V6) ? AF_INET6 : AF_INET;
    int rc = -1;

    if (interface) {
        rc = getnamed_ifaddr (buf, len, interface, prefer_family, error);
    }
    else {
        if (!(flags & IPADDR_HOSTNAME))
            rc = getprimary_ifaddr (buf, len, prefer_family, error);
        if (rc < 0) {
            rc = getprimary_hostaddr (buf, len, prefer_family, error);
        }
    }
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
