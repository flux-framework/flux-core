/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_IPADDR_H
#define _UTIL_IPADDR_H

#include <sys/socket.h> // for AF_INET, AF_INET6
#include "src/common/libflux/types.h" // for flux_error_t

enum {
    IPADDR_V6 = 1,
    IPADDR_HOSTNAME = 2,
};

/* Guess at a usable network address for the local node using one
 * of these methods:
 * 1. Find the interface associated with the default route, then
 *    look up address of that interface.
 * 2. Look up address associated with the hostname
 * 3. Look up address associated with a specific interface.
 *
 * Main use case: determine bind address for a PMI-bootstrapped flux broker.
 *
 * Flags and the optional 'interface' param alter the default behavior:
 *
 * IPADDR_IPV6
 *   if set, IPv6 addresses are preferred, with fallback to IPv4
 *   if unset, IPv4 addresses are preferred, with fallback to IPv6
 * IPADDR_HOSTNAME
 *   if set, only method 2 is tried above
 *   if unset, first method 1 is tried, then if that fails, method 2 is tried
 * 'interface'
 *   if set, only method 3 is tried above
 *
 * Return address as a string in buf (up to len bytes, always null terminated)
 * Return 0 on success, -1 on error with error message written to 'error'
 * if non-NULL.
 */
int ipaddr_getprimary (char *buf,
                       int len,
                       int flags,
                       const char *interface,
                       flux_error_t *error);

#endif /* !_UTIL_IPADDR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
