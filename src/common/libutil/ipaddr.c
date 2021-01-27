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
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
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

int ipaddr_getprimary (char *buf, int len, char *errstr, int errstrsz)
{
    char hostname[HOST_NAME_MAX + 1];
    struct addrinfo hints, *res = NULL;
    int e;

    if (gethostname (hostname, sizeof (hostname)) < 0) {
        esprintf (errstr, errstrsz, "gethostname: %s", strerror (errno));
        return -1;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((e = getaddrinfo (hostname, NULL, &hints, &res)) || res == NULL) {
        esprintf (errstr, errstrsz, "getaddrinfo %s: %s",
                  hostname, gai_strerror (e));
        return -1;
    }
    if ((e = getnameinfo (res->ai_addr, res->ai_addrlen, buf, len,
                          NULL, 0, NI_NUMERICHOST))) {
        esprintf (errstr, errstrsz, "getnameinfo: %s", gai_strerror (e));
        freeaddrinfo (res);
        return -1;
    }
    freeaddrinfo (res);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
