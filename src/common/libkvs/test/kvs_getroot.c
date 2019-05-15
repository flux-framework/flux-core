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

#include "src/common/libflux/flux.h"
#include "kvs_getroot.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    flux_t *h = (flux_t *)(uintptr_t)42;  // fake but non-NULL
    flux_future_t *f;
    const char *s;
    int i;
    uint32_t u32;

    /* check simple error cases */
    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    errno = 0;
    ok (flux_kvs_getroot (NULL, "foo", 0) == NULL && errno == EINVAL,
        "flux_kvs_getroot h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot (h, "foo", 0xff) == NULL && errno == EINVAL,
        "flux_kvs_getroot flags=(inval) fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_blobref (NULL, &s) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_blobref f=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_blobref (f, NULL) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_blobref blobref=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_sequence (NULL, &i) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_sequence f=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_sequence (f, NULL) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_sequence sequence=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_owner (NULL, &u32) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_owner f=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_owner (f, NULL) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_owner owner=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_treeobj (NULL, &s) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_treeobj f=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_treeobj (f, NULL) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_treeobj treeobj=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_getroot_get_treeobj (f, &s) < 0 && errno == EINVAL,
        "flux_kvs_getroot_get_treeobj f=(non-getroot) fails with EINVAL");

    flux_future_destroy (f);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errors ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
