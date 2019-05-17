/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

#include <string.h>
#include <ctype.h>

int main (int argc, char** argv)
{
    char buf[WALLCLOCK_MAXLEN];
    plan (NO_PLAN);

    ok (wallclock_get_zulu (buf, sizeof (buf)) >= 0,
        "wallclock_get_zulu() works: %s",
        buf);
    ok (strlen (buf) < WALLCLOCK_MAXLEN,
        "result did not overflow WALLCLOCK_MAXLEN");
    ok (strlen (buf) < STDLOG_MAX_TIMESTAMP,
        "result did not overflow STDLOG_MAX_TIMESTAMP");

    /* example: 2016-06-10T18:01:18.479194Z */

    ok (buf[10] == 'T',
        "RFC 5424: mandatory T character present in correct position");
    ok (strchr (buf, 'Z') != NULL || strchr (buf, 'z') == NULL,
        "RFC 5424: optional Z character is upper case");

    int count = 0;
    char* p = strchr (buf, '.');
    while (p && isdigit (*(++p)))
        count++;
    ok (count <= 6, "RFC 5424: no more than 6 optional TIME-SECFRAC digits");

    done_testing ();
}
