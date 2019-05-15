/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <sys/param.h>
#include <argz.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/ipaddr.h"

int main (int argc, char **argv)
{
    char host[MAXHOSTNAMELEN + 1];
    char errstr[200];
    char *addrs = NULL;
    size_t addrs_len = 0;
    const char *entry = NULL;

    plan (NO_PLAN);

    ok (ipaddr_getprimary (host, sizeof (host), errstr, sizeof (errstr)) == 0,
        "ipaddr_getprimary works");
    diag ("primary: %s", host);

    ok (ipaddr_getall (&addrs, &addrs_len, errstr, sizeof (errstr)) == 0,
        "ipaddrs_getall works");
    while ((entry = argz_next (addrs, addrs_len, entry)))
        diag ("%s", entry);

    free (addrs);

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
