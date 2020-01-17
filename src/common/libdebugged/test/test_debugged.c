/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"
#include "src/common/libdebugged/debugged.h"

void test_initial_state (void)
{
    int debugged = get_mpir_being_debugged ();
    ok (debugged == 0,
        "mpir_being_debugged is initially 0");
    set_mpir_being_debugged (1);
    debugged = get_mpir_being_debugged ();
    ok (debugged == 1,
        "mpir_being_debugged is set to 1 (e.g., under debugger control)");
    set_mpir_being_debugged (0);
    debugged = get_mpir_being_debugged ();
    ok (debugged == 0,
        "mpir_being_debugged is unset to 0 (e.g., debugger detached)");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_initial_state ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
