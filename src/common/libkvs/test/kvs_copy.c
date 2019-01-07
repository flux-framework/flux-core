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

int main (int argc, char *argv[])
{
    flux_t *h = (flux_t *)(uintptr_t)42;

    plan (NO_PLAN);

    errno = 0;
    ok (flux_kvs_copy (NULL, "a", NULL, "b", NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_copy h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_copy (h, NULL, NULL, "b", NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_copy srckey=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_copy (h, "a", NULL, NULL, NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_copy srckey=NULL fails with EINVAL");

    errno = 0;
    ok (flux_kvs_move (NULL, "a", NULL, "b", NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_move h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_move (h, NULL, NULL, "b", NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_move srckey=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_move (h, "a", NULL, NULL, NULL, 0) == NULL
        && errno == EINVAL,
        "flux_kvs_move srckey=NULL fails with EINVAL");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

