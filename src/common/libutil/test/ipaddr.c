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

int main(int argc, char** argv)
{
    char host[MAXHOSTNAMELEN + 1];
    char errstr[200];
    int n;

    plan (NO_PLAN);

    memset (errstr, 0, sizeof (errstr));
    n = ipaddr_getprimary (host, sizeof (host), errstr, sizeof (errstr));
    ok (n == 0,
        "ipaddr_getprimary works");
    if (n == 0)
        diag ("primary: %s", host);
    else
        diag ("Error: %s", errstr);

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
