/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#ifndef EDEADLOCK
#define EDEADLOCK EDEADLK
#endif

#include "src/common/libtap/tap.h"

#include "kvs_checkpoint.h"

void errors (void)
{
    flux_future_t *f;
    const char *rootref;

    errno = 0;
    ok (kvs_checkpoint_commit (NULL, NULL, 0, 0, -1) == NULL
        && errno == EINVAL,
        "kvs_checkpoint_commit fails on bad input");

    errno = 0;
    ok (kvs_checkpoint_lookup (NULL, -1) == NULL
        && errno == EINVAL,
        "kvs_checkpoint_lookup fails on bad input");

    errno = 0;
    ok (kvs_checkpoint_lookup_get_rootref (NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_checkpoint_lookup_get_rootref fails on bad input");

    errno = 0;
    ok (kvs_checkpoint_lookup_get_timestamp (NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_checkpoint_lookup_get_timestamp fails on bad input");

    errno = 0;
    ok (kvs_checkpoint_lookup_get_sequence (NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_checkpoint_lookup_get_sequence fails on bad input");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    errno = 0;
    ok (kvs_checkpoint_lookup_get_rootref (f, &rootref) < 0
        && errno == EDEADLOCK,
        "kvs_checkpoint_lookup_get_rootref fails on unfulfilled future");

    errno = 0;
    double timestamp;
    ok (kvs_checkpoint_lookup_get_timestamp (f, &timestamp) < 0
        && errno == EDEADLOCK,
        "kvs_checkpoint_lookup_get_timestamp fails on unfulfilled future");

    errno = 0;
    int sequence;
    ok (kvs_checkpoint_lookup_get_sequence (f, &sequence) < 0
        && errno == EDEADLOCK,
        "kvs_checkpoint_lookup_get_sequence fails on unfulfilled future");

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

