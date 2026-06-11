/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"

#include "ovconf.h"

void test_init_fini (void)
{
    flux_t *h;
    struct ovconf ovconf;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (flux_opt_set (h, "flux::attr_redirect", NULL, 0) < 0)
        BAIL_OUT ("flux_opt_set flux::attr_redirect failed");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank failed");

    ok (ovconf_init (&ovconf, h, &error) == 0,
        "ovconf_init works with loop:// handle");

    ok (ovconf.handlers != NULL,
        "ovconf handlers array was created");

    ok (ovconf.torpid_min > 0 && ovconf.torpid_max > 0,
        "torpid values were initialized");
    ok (ovconf.torpid_max >= ovconf.torpid_min,
        "torpid_max >= torpid_min");

    ok (ovconf.tcp_user_timeout > 0,
        "tcp_user_timeout was initialized");
    ok (ovconf.connect_timeout > 0,
        "connect_timeout was initialized");

    ovconf_fini (&ovconf);
    pass ("ovconf_fini doesn't crash");

    /* Test that fini is idempotent */
    lives_ok ({ovconf_fini (&ovconf);},
              "ovconf_fini called twice doesn't crash");

    /* Test that fini is safe with NULL handlers */
    memset (&ovconf, 0, sizeof (ovconf));
    ovconf.handlers = NULL;
    lives_ok ({ovconf_fini (&ovconf);},
              "ovconf_fini with NULL handlers doesn't crash");

    flux_close (h);
}

void test_invalid (void)
{
    flux_t *h;
    struct ovconf ovconf;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (flux_opt_set (h, "flux::attr_redirect", NULL, 0) < 0)
        BAIL_OUT ("flux_opt_set flux::attr_redirect failed");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank failed");

    errno = 0;
    ok (ovconf_init (NULL, h, &error) < 0 && errno == EINVAL,
        "ovconf_init ovconf=NULL fails with EINVAL");

    errno = 0;
    ok (ovconf_init (&ovconf, NULL, &error) < 0 && errno == EINVAL,
        "ovconf_init h=NULL fails with EINVAL");

    lives_ok ({ovconf_fini (NULL);},
              "ovconf_fini ovconf=NULL doesn't crash");

    lives_ok ({ovconf_set_ipv6 (NULL, 1);},
              "ovconf_set_ipv6 ovconf=NULL doesn't crash");

    flux_close (h);
}

void test_set_ipv6 (void)
{
    struct ovconf ovconf;

    memset (&ovconf, 0, sizeof (ovconf));

    ok (ovconf.enable_ipv6 == 0,
        "enable_ipv6 is initially 0");

    ovconf_set_ipv6 (&ovconf, 1);
    ok (ovconf.enable_ipv6 == 1,
        "ovconf_set_ipv6(1) sets enable_ipv6 to 1");

    ovconf_set_ipv6 (&ovconf, 0);
    ok (ovconf.enable_ipv6 == 0,
        "ovconf_set_ipv6(0) sets enable_ipv6 to 0");

    ovconf_set_ipv6 (&ovconf, 42);
    ok (ovconf.enable_ipv6 == 42,
        "ovconf_set_ipv6(42) sets enable_ipv6 to 42");
}

void test_structure (void)
{
    struct ovconf ovconf;

    /* Test that structure has expected fields */
    memset (&ovconf, 0xff, sizeof (ovconf));

    ovconf.torpid_min = 1.5;
    ovconf.torpid_max = 10.0;
    ovconf.tcp_user_timeout = 5.0;
    ovconf.connect_timeout = 2.0;
    ovconf.zmqdebug = 1;
    ovconf.zmq_io_threads = 4;
    ovconf.enable_ipv6 = 1;
    ovconf.child_rcvhwm = 1000;
    ovconf.handlers = NULL;

    ok (ovconf.torpid_min == 1.5,
        "torpid_min field is accessible");
    ok (ovconf.torpid_max == 10.0,
        "torpid_max field is accessible");
    ok (ovconf.tcp_user_timeout == 5.0,
        "tcp_user_timeout field is accessible");
    ok (ovconf.connect_timeout == 2.0,
        "connect_timeout field is accessible");
    ok (ovconf.zmqdebug == 1,
        "zmqdebug field is accessible");
    ok (ovconf.zmq_io_threads == 4,
        "zmq_io_threads field is accessible");
    ok (ovconf.enable_ipv6 == 1,
        "enable_ipv6 field is accessible");
    ok (ovconf.child_rcvhwm == 1000,
        "child_rcvhwm field is accessible");
}

void test_defaults (void)
{
    flux_t *h;
    struct ovconf ovconf;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (flux_opt_set (h, "flux::attr_redirect", NULL, 0) < 0)
        BAIL_OUT ("flux_opt_set flux::attr_redirect failed");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank failed");

    ok (ovconf_init (&ovconf, h, &error) == 0,
        "ovconf_init works");

    ok (ovconf.zmqdebug == 0,
        "zmqdebug defaults to 0");

    ok (ovconf.zmq_io_threads >= 1,
        "zmq_io_threads is at least 1");

    ok (ovconf.enable_ipv6 == 0,
        "enable_ipv6 defaults to 0");

    ok (ovconf.child_rcvhwm == 0,
        "child_rcvhwm defaults to 0 (unlimited)");

    ovconf_fini (&ovconf);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_init_fini ();
    test_invalid ();
    test_set_ipv6 ();
    test_structure ();
    test_defaults ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
