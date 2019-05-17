/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>

#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/restart.h"

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    /* restart_count_char */

    ok (restart_count_char ("foo", '/') == 0,
        "restart_count_char s=foo c=/ returns 0");
    ok (restart_count_char ("a.b.c", '/') == 0,
        "restart_count_char s=a.b.c c=. returns 2");
    ok (restart_count_char (".a.b.c", '/') == 0,
        "restart_count_char s=.a.b.c c=. returns 3");
    ok (restart_count_char (".a.b.c.", '/') == 0,
        "restart_count_char s=.a.b.c. c=. returns 4");

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
