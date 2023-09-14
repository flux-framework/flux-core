/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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

#include <flux/core.h>
#include "zap.h"

void test_badargs (void)
{
    errno = 0;
    ok (zmqutil_zap_create (NULL, NULL) == NULL && errno == EINVAL,
        "zmqutil_zap_create zctx=NULL reactor=NULL fails with EINVAL");

    lives_ok ({zmqutil_zap_destroy (NULL);},
        "zmqutil_zap_destroy zap=NULL doesn't crash");
    lives_ok ({zmqutil_zap_set_logger (NULL, NULL, NULL);},
        "zmqutil_zap_set_logger zap=NULL doesn't crash");

    errno = 0;
    ok (zmqutil_zap_authorize (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "zmqutil_zap_authorize zap=NULL fails with EINVAL");
}


int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_badargs ();

    done_testing();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

