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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/ipaddr.h"


int main(int argc, char** argv)
{
    char host[MAXHOSTNAMELEN + 1];
    flux_error_t error;
    int n;

    plan (NO_PLAN);

    n = ipaddr_getprimary (host, sizeof (host), 0, NULL, &error);
    ok (n == 0,
        "ipaddr_getprimary (hostname=0 v6=0) works");
    diag ("%s", n == 0 ? host : error.text);

    n = ipaddr_getprimary (host, sizeof (host), IPADDR_V6, NULL, &error);
    ok (n == 0,
        "ipaddr_getprimary (hostname=0 v6=1) works");
    diag ("%s", n == 0 ? host : error.text);

    n = ipaddr_getprimary (host, sizeof (host), IPADDR_HOSTNAME, NULL, &error);
    ok (n == 0,
        "ipaddr_getprimary (hostname=1 v6=0) works");
    diag ("%s", n == 0 ? host : error.text);

    n = ipaddr_getprimary (host,
                           sizeof (host),
                           IPADDR_HOSTNAME | IPADDR_V6,
                           NULL,
                           &error);
    ok (n == 0,
        "ipaddr_getprimary (hostname=1 v6=1)");
    diag ("%s", n == 0 ? host : error.text);

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
