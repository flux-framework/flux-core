/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* cidr.c - parse RFC 4632 CIDR notation */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "errno_safe.h"
#include "cidr.h"

/* Destructively parse /N from the end of 's'.
 * Leaves 's' containing only the string before the / character.
 * If /N is missing, return max_value.
 */
static int parse_netprefix (char *s, int max_value)
{
    char *cp;
    char *endptr;
    int n = max_value;

    if ((cp = strrchr (s, '/'))) {
        *cp++ = '\0';
        errno = 0;
        n = strtoul (cp, &endptr, 10);
        if (errno != 0 || *endptr != '\0' || n > max_value) {
            errno = EINVAL;
            return -1;
        }
    }
    return n;
}

static uint32_t netprefix_to_netmask4 (int prefix)
{
    if (prefix > 0)
        return htonl (~((1 << (32 - prefix)) - 1));
    return 0;
}

int cidr_parse4 (struct cidr4 *cidrp, const char *s)
{
    char *cpy;
    struct cidr4 cidr;
    int prefix;
    int n;

    if (!cidrp || !s) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (s)))
        return -1;
    if ((prefix = parse_netprefix (cpy, 32)) < 0)
        goto error;
    cidr.mask.s_addr = netprefix_to_netmask4 (prefix);
    if ((n = inet_pton (AF_INET, cpy, &cidr.addr)) < 0)
        goto error;
    if (n == 0) {
        errno = EINVAL;
        goto error;
    }

    free (cpy);
    *cidrp = cidr;
    return 0;
error:
    ERRNO_SAFE_WRAP (free, cpy);
    return -1;
}

bool cidr_match4 (struct cidr4 *cidr, struct in_addr *addr)
{
    if (!cidr || !addr)
        return false;
    uint32_t mask = cidr->mask.s_addr;
    if ((addr->s_addr & mask) == (cidr->addr.s_addr & mask))
        return true;
    return false;
}

// vi:ts=4 sw=4 expandtab
