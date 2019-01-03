/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libflux/version.h"
#include "src/common/libtap/tap.h"

#include <string.h>

int main (int argc, char *argv[])
{
    const char *s;
    int a,b,c,d;
    char vs[32];

    plan (NO_PLAN);

    d = flux_core_version (&a, &b, &c);
    ok (d == (a<<16 | b<<8 | c),
        "flux_core_version returned sane value");

    lives_ok ({flux_core_version (NULL, NULL, NULL);},
        "flux_core_version NULL, NULL, NULL doesn't crash");

    snprintf (vs, sizeof (vs), "%d.%d.%d", a,b,c);
    s = flux_core_version_string ();
    ok (s != NULL && !strncmp (s, vs, strlen (vs)),
        "flux_core_version_string returned expected string");
    diag (s);



    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

