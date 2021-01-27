/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_GETIP_H
#define _UTIL_GETIP_H

#include <sys/types.h>

/* Get ip address associated with primary hostname.
 * Return it as a string in buf (up to len bytes, always null terminated)
 * Return 0 on success, -1 on error with error message written to errstr
 * if non-NULL.
 */
int ipaddr_getprimary (char *buf, int len, char *errstr, int errstrsz);

#endif /* !_UTIL_GETIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
