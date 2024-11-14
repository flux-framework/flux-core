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
#include <errno.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

void errors (void)
{
    flux_future_t *f;
    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_lookup (NULL, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_kvs_lookup fails on bad input");

    errno = 0;
    ok (flux_kvs_lookupat (NULL, 0, NULL, NULL) == NULL && errno == EINVAL,
        "flux_kvs_lookupat fails on bad input");

    errno = 0;
    ok (flux_kvs_lookup_get (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_lookup_get fails on bad input");

    errno = 0;
    ok (flux_kvs_lookup_get_unpack (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_lookup_get_unpack fails on bad input");

    errno = 0;
    ok (flux_kvs_lookup_get_raw (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_lookup_get_raw fails on bad input");

    errno = 0;
    ok (flux_kvs_lookup_get_key (NULL) == NULL && errno == EINVAL,
        "flux_kvs_lookup_get_key future=NULL fails with EINVAL");

    errno = 0;
    ok (flux_kvs_lookup_cancel (NULL) == -1 && errno == EINVAL,
        "flux_kvs_lookup_cancel future=NULL fails with EINVAL");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    errno = 0;
    ok (flux_kvs_lookup_get_key (f) == NULL && errno == EINVAL,
        "flux_kvs_lookup_get_key future=(wrong type) fails with EINVAL");

    errno = 0;
    ok (flux_kvs_lookup_cancel (f) == -1 && errno == EINVAL,
        "flux_kvs_lookup_cancel future=(wrong type) fails with EINVAL");

    flux_future_destroy (f);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    errors ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

