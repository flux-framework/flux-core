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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

void test_debug (void)
{
    flux_t *h;
    struct flux_handle_ops ops;
    int flags;

    /* Create dummy handle with no capability - only aux hash */
    memset (&ops, 0, sizeof (ops));
    if (!(h = flux_handle_create (NULL, &ops, 0)))
        BAIL_OUT ("flux_handle_create failed");

    ok (flux_module_debug_test (h, 1, false) == false,
        "flux_module_debug_test returns false with unpopulated aux");

    if (flux_aux_set (h, "flux::debug_flags", &flags, NULL) < 0)
        BAIL_OUT ("flux_aux_set failed");

    flags = 0x0f;
    ok (flux_module_debug_test (h, 0x10, false) == false,
        "flux_module_debug_test returns false on false flag (clear=false)");
    ok (flux_module_debug_test (h, 0x01, false) == true,
        "flux_module_debug_test returns true on true flag (clear=false)");
    ok (flags == 0x0f,
        "flags are unaltered after testing with clear=false");

    ok (flux_module_debug_test (h, 0x01, true) == true,
        "flux_module_debug_test returns true on true flag (clear=true)");
    ok (flags == 0x0e,
        "flag was cleared after testing with clear=true");

    flux_handle_destroy (h);
}

void test_set_running (void)
{
    flux_t *h;

    if (!(h = loopback_create (0)))
        BAIL_OUT ("loopback_create failed");

    ok (flux_module_set_running (h) == 0,
        "flux_module_set_running returns success");
    errno = 0;
    ok (flux_module_set_running (NULL) < 0 && errno == EINVAL,
        "flux_module_set_running h=NULL fails with EINVAL");

    flux_close (h);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_debug ();
    test_set_running ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

