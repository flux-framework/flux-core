/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>

#include "src/common/libtap/tap.h"
#include "rnode.h"

static void rnode_alloc_and_check (struct rnode *n, int count, char *expected)
{
    struct idset *ids = NULL;
    char *result = NULL;
    int avail = rnode_avail (n);
    ok (rnode_alloc (n, count, &ids) == 0,
        "rnode_alloc: count=%d", count);
    ok (ids != NULL,
        "rnode_alloc: returns non-null idset");
    ok (idset_count (ids) == count,
        "rnode_alloc: returned idset with expected count (%d)",
        idset_count (ids));
    if (!(result = idset_encode (ids, IDSET_FLAG_RANGE)))
        BAIL_OUT ("failed to encode idset result");
    is (result, expected,
        "rnode_alloc: count=%d: returned expected result %s", count, result);
    ok (rnode_avail (n) == avail - count,
        "rnode_alloc: rnode_avail now %d, expected %d",
        rnode_avail (n), avail - count);
    idset_destroy (ids);
    free (result);
}

static void rnode_avail_check (struct rnode *n, const char *expected)
{
    char *avail = idset_encode (n->avail, IDSET_FLAG_RANGE);
    if (avail == NULL)
        BAIL_OUT ("failed to encode n->avail");
    is (avail, expected,
        "rnode->avail is expected: %s", avail);
    free (avail);
}

int main (int ac, char *av[])
{
    struct idset *ids = NULL;
    struct rnode *n = NULL;

    plan (NO_PLAN);

    if (!(n = rnode_create (0, "0-3")))
        BAIL_OUT ("could not create an rnode object");
    ok (rnode_avail (n) == 4,
        "rnode_avail == 4");

    ok (rnode_alloc (n, 5, &ids) < 0 && errno == ENOSPC,
        "rnode_alloc too many cores returns errno ENOSPC");

    rnode_alloc_and_check (n, 1, "0");
    ok (rnode_avail (n) == 3,
        "rnode_avail == 3");
    rnode_avail_check (n, "1-3");

    rnode_alloc_and_check (n, 1, "1");
    ok (rnode_avail (n) == 2,
        "rnode_avail == 2");
    rnode_avail_check (n, "2-3");

    rnode_alloc_and_check (n, 2, "2-3");
    ok (rnode_avail (n) == 0,
        "rnode_avail == 0");
    rnode_avail_check (n, "");

    ok (rnode_alloc (n, 1, &ids) < 0 && errno == ENOSPC && ids == NULL,
        "rnode_alloc on empty rnode fails with ENOSPC");

    ok (rnode_free (n, "3-4") < 0 && errno == ENOENT,
        "rnode_free with invalid ids fails");
    ok (rnode_avail (n) == 0,
        "rnode_avail still is 0");
    rnode_avail_check (n, "");

    ok (rnode_free (n, "0-1") == 0,
        "rnode_free (0-1) works");
    ok (rnode_avail (n) == 2,
        "rnode_avail now is 2");
    rnode_avail_check (n, "0-1");
    ok (rnode_free (n, "0") < 0 && errno == EEXIST,
        "rnode_free of already available id fails");
    ok (rnode_avail (n) == 2,
        "rnode_avail is still 2");
    ok (rnode_free (n, "3") == 0,
        "rnode_free '3' works");
    rnode_avail_check (n, "0-1,3");

    rnode_alloc_and_check (n, 3, "0-1,3");

    rnode_destroy (n);

    n = rnode_create_count (1, 8);
    if (n == NULL)
        BAIL_OUT ("rnode_create_count failed");
    ok (n->rank == 1, "rnode rank set correctly");
    ok (n != NULL, "rnode_create_count");
    rnode_avail_check (n, "0-7");
    rnode_destroy (n);

    struct idset *idset = idset_decode ("0-3");
    n = rnode_create_idset (3, idset);
    idset_destroy (idset);
    if (n == NULL)
        BAIL_OUT ("rnode_create_idset failed");
    ok (n != NULL, "rnode_create_idset");
    ok (n->rank == 3, "rnode rank set correctly");
    rnode_avail_check (n, "0-3");

    struct idset *alloc = idset_decode ("1,3");
    ok (rnode_alloc_idset (n, alloc) == 0,
        "rnode_alloc_idset (1,3)");
    rnode_avail_check (n, "0,2");
    ok (rnode_alloc_idset (n, alloc) < 0 && errno == EEXIST,
        "rnode_alloc_idset with idset already allocated returns EEXIST");

    ok (rnode_free_idset (n, alloc) == 0,
        "rnode_free_idset (1,3)");
    rnode_avail_check (n, "0-3");

    ok (rnode_free_idset (n, alloc) < 0 && errno == EEXIST,
        "rnode_free_idset with idset already available returns EEXIST");

    idset_destroy (alloc);
    alloc = idset_decode ("4-7");
    ok (rnode_alloc_idset (n, alloc) < 0 && errno == ENOENT,
        "rnode_alloc_idset with invalid ids return ENOENT");
    ok (rnode_free_idset (n, alloc) < 0 && errno == ENOENT,
        "rnode_free_idset with invalid ids return ENOENT");

    idset_destroy (alloc);
    rnode_destroy (n);
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
