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
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

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
    ok (s != NULL && strstarts (s, vs),
        "flux_core_version_string returned expected string");
    diag (s);



    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

