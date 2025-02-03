/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "list.h"

void test_inval (void)
{
    flux_t *h;
    struct unit_info info;
    flux_future_t *f;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(f = flux_future_create (NULL, 0)))
        BAIL_OUT ("could not create future for testing");

    errno = 0;
    ok (sdexec_list_units (NULL, "sdexec", 0, "*") == NULL && errno == EINVAL,
        "sdexec_list_units h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_list_units (h, NULL, 0, "*") == NULL && errno == EINVAL,
        "sdexec_list_units service=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_list_units (h, "sdexec", 0, NULL) == NULL && errno == EINVAL,
        "sdexec_list_units pattern=NULL fails with EINVAL");

    ok (sdexec_list_units_next (NULL, &info) == false,
        "sdexec_list_units_next f=NULL returns false");
    ok (sdexec_list_units_next (f, NULL) == false,
        "sdexec_list_units_next infop=NULL returns false");

    flux_future_destroy (f);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
