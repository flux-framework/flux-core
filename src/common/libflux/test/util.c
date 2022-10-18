/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libflux/util.h"
#include "src/common/libtap/tap.h"

#include <string.h>

int main (int argc, char *argv[])
{
    int ret;

    plan (NO_PLAN);

    ret = flux_get_process_scope (NULL);
    ok (ret < 0 && errno == EINVAL,
        "flux_get_process_scope returns EINVAL on invalid input");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

