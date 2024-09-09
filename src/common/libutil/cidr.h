/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_CIDR_H
#define _UTIL_CIDR_H

#include <arpa/inet.h>
#include <stdbool.h>

struct cidr4 {
    struct in_addr addr;
    struct in_addr mask;
};

int cidr_parse4 (struct cidr4 *cidr, const char *s);
bool cidr_match4 (struct cidr4 *cidr, struct in_addr *addr);

#endif /* !_UTIL_CIDR_H */

// vi:ts=4 sw=4 expandtab
