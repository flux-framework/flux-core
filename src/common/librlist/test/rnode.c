/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
    char *avail = idset_encode (n->cores->avail, IDSET_FLAG_RANGE);
    if (avail == NULL)
        BAIL_OUT ("failed to encode n->cores->avail");
    is (avail, expected,
        "rnode->avail is expected: %s", avail);
    free (avail);
}

static void test_diff ()
{
    struct rnode *a = rnode_create ("foo", 0, "0-3");
    struct rnode *b = rnode_create ("foo", 0, "0-3");
    struct rnode *c = rnode_create ("foo", 0, "0-1");
    struct rnode *result = NULL;

    if (!a || !b || !c)
        BAIL_OUT ("rnode_create failed!");

    result = rnode_diff (a, b);
    ok (result != NULL,
        "rnode_diff (a, b) worked");
    ok (rnode_empty (result),
        "result is empty");
    rnode_destroy (result);

    result = rnode_diff (a, c);
    ok (result != NULL,
        "rnode_diff (a, c) works");
    ok (!rnode_empty (result),
        "rnode is not empty");
    ok (rnode_avail_total (result) == 2,
        "result has two available resources");
    rnode_avail_check (result, "2-3");
    rnode_destroy (result);

    result = rnode_diff_ex (a, c, RNODE_IGNORE_CORE);
    ok (result != NULL,
        "rnode_diff_ex (a, c, RNODE_IGNORE_CORE) works");
    diag ("result: %d cores %d gpus",
          rnode_count_type (result, "core"),
          rnode_count_type (result, "gpu"));
    ok (rnode_empty (result),
        "result is empty");
    rnode_destroy (result);

    diag ("adding one gpu to rnode a");
    if (!rnode_add_child (a, "gpu", "0"))
        BAIL_OUT ("rnode_add_child failed");

    diag ("rnode a: %d cores %d gpus",
          rnode_count_type (a, "core"),
          rnode_count_type (a, "gpu"));

    result = rnode_diff (a, b);
    ok (result != NULL,
        "rnode_diff (a, b) works");
    diag ("result: %d cores %d gpus",
          rnode_count_type (result, "core"),
          rnode_count_type (result, "gpu"));
    ok (!rnode_empty (result),
        "rnode is not empty");
    diag ("result has %d total resources",
          rnode_avail_total (result));
    ok (rnode_count_type (result, "gpu") == 1,
        "result has one available gpu");
    rnode_destroy (result);

    result = rnode_diff_ex (a, b, RNODE_IGNORE_GPU);
    ok (result != NULL,
        "rnode_diff_ex (a, b, RNODE_IGNORE_GPU) works");
    diag ("result: %d cores %d gpus",
          rnode_count_type (result, "core"),
          rnode_count_type (result, "gpu"));
    ok (rnode_empty (result),
        "rnode is empty");
    rnode_destroy (result);

    result = rnode_diff_ex (a, c, RNODE_IGNORE_CORE);
    ok (result != NULL,
        "rnode_diff_ex (a, c, RNODE_IGNORE_CORE) works");
    diag ("result: %d cores %d gpus",
          rnode_count_type (result, "core"),
          rnode_count_type (result, "gpu"));
    ok (rnode_count_type (result, "gpu") == 1,
        "result has one available gpu");
    rnode_destroy (result);

    result = rnode_diff_ex (a, c, RNODE_IGNORE_GPU);
    ok (result != NULL,
        "rnode_diff_ex (a, c, RNODE_IGNORE_GPU) works");
    ok (!rnode_empty (result),
        "rnode is not empty");
    diag ("result: %d cores %d gpus",
          rnode_count_type (result, "core"),
          rnode_count_type (result, "gpu"));
    rnode_avail_check (result, "2-3");
    ok (rnode_count_type (result, "gpu") == 0,
        "result has no available gpus");
    rnode_destroy (result);

    rnode_destroy (a);
    rnode_destroy (b);
    rnode_destroy (c);
}

static void test_intersect ()
{
    struct rnode *a = rnode_create ("foo", 0, "0-1");
    struct rnode *b = rnode_create ("foo", 0, "1-3");
    struct rnode *c = rnode_create ("foo", 0, "2-3");
    struct rnode *result = NULL;

    if (!a || !b || !c)
        BAIL_OUT ("rnode_create failed");

    result = rnode_intersect (a, b);
    ok (result != NULL,
        "rnode_intersect (a, b) worked");
    if (!result)
        BAIL_OUT ("rnode_intersect failed: %s", strerror (errno));
    ok (!rnode_empty (result),
        "result is not empty");
    ok (rnode_count_type (result, "core") == 1,
        "result has 1 core");
    rnode_destroy (result);

    result = rnode_intersect (a, c);
    ok (result != NULL,
        "rnode_intersect (a, c) worked");
    if (!result)
        BAIL_OUT ("rnode_intersect failed: %s", strerror (errno));
    ok (rnode_empty (result),
        "result is empty");
    rnode_destroy (result);

    rnode_destroy (a);
    rnode_destroy (b);
    rnode_destroy (c);
}

static void test_add_child ()
{
    struct rnode_child *c;
    struct rnode *a = rnode_create ("foo", 0, "0-3");

    if (!a)
        BAIL_OUT ("rnode_create failed");

    ok (rnode_count (a) == 4,
        "rnode_create worked");
    ok (rnode_count_type (a, "gpu") == 0,
        "rnode_count_type (gpu) == 0");

    if (!(c = rnode_add_child (a, "gpu", "0")))
        BAIL_OUT ("rnode_add_child failed");

    is (c->name, "gpu",
        "rnode_add_child (gpu) works");
    ok (idset_count (c->ids) == 1 && idset_count (c->avail) == 1,
        "child has correct idsets");
    ok (rnode_count_type (a, "gpu") == 1,
        "rnode_count_type (gpu) == 1");

    if (!(c = rnode_add_child (a, "core", "4-7")))
        BAIL_OUT ("rnode_add_child failed");

    is (c->name, "core",
        "rnode_add_child (core) works");
    ok (rnode_count (a) == 8,
        "core count is now 8");
    ok (rnode_avail_total (a) == 9,
        "total available resources is 9");

    ok (rnode_add_child (a, "gpu", "0-1") == NULL && errno == EEXIST,
        "rnode_add_child fails with EEXIST if ids already exist in set");

    rnode_destroy (a);
}

void test_copy ()
{
    struct rnode *n = NULL;
    struct rnode *b = NULL;

    if (!(n = rnode_create ("foo", 0, "0-3")))
        BAIL_OUT ("failed to create an rnode object");
    ok (rnode_add_child (n, "gpu", "0-1") != NULL,
        "add two gpus to rnode");

    ok ((b = rnode_copy (n)) != NULL,
        "copy rnode");
    ok (rnode_count_type (b, "core") == 4,
        "rnode_count_type (gpu) == 4");
    ok (rnode_count_type (b, "gpu") == 2,
        "rnode_count_type (gpu) == 2");

    rnode_destroy (b);
    ok ((b = rnode_copy_avail (n)) != NULL,
        "rnode_copy_avail");
    ok (rnode_count_type (b, "core") == 4,
        "rnode_count_type (gpu) == 4");
    ok (rnode_count_type (b, "gpu") == 2,
        "rnode_count_type (gpu) == 2");

    rnode_destroy (b);
    ok ((b = rnode_copy_cores (n)) != NULL,
        "copy rnode (cores only)");
    ok (rnode_count_type (b, "core") == 4,
        "rnode_count_type (gpu) == 4");
    ok (rnode_count_type (b, "gpu") == 0,
        "rnode_count_type (gpu) == 0");

    rnode_destroy (b);
    rnode_destroy (n);
}

void test_rnode_cmp ()
{
    struct rnode *a = NULL;
    struct rnode *b = NULL;

    if (!(a = rnode_create ("foo", 0, "0-3"))
        || !(b = rnode_create ("foo", 1, "0-3")))
        BAIL_OUT ("failed to create rnode objects");

    ok (rnode_cmp (a, b) == 0,
        "rnode_cmp returns zero for nodes with identical children");

    /*  Add gpus to rnode b only */
    ok (rnode_add_child (b, "gpu", "0-1") != NULL,
        "add two gpus to rnode");

    ok (rnode_cmp (a, b) != 0,
        "rnode_cmp returns nonzero for nodes with differing children");

    rnode_destroy (a);
    rnode_destroy (b);
}

void test_properties ()
{
    struct rnode *a;
    struct rnode *b;

    if (!(a = rnode_create ("foo", 0, "0-3")))
        BAIL_OUT ("failed to create rnode object");

    ok (rnode_set_property (a, "blingy") == 0,
        "rnode_set_property works");
    ok (rnode_set_property (a, "blingy") == 0,
        "rnode_set_property again works");
    ok (rnode_has_property (a, "blingy"),
        "rnode_has_property works");
    ok (!rnode_has_property (a, "dull"),
        "rnode_has_property returns false if property not set");
    if (!(b = rnode_copy (a)))
        BAIL_OUT ("failed to copy rnode");
    ok (b != NULL,
        "rnode_copy with properties");
    ok (rnode_has_property (b, "blingy"),
        "rnode_has_property works on copy");
    ok (!rnode_has_property (b, "dull"),
        "rnode_has_property on copy returns false if property not set");
    rnode_remove_property (a, "blingy");
    ok (!rnode_has_property (a, "blingy"),
        "rnode_has_property now returns false");

    rnode_destroy (a);
    rnode_destroy (b);
}

int main (int ac, char *av[])
{
    struct idset *ids = NULL;
    struct rnode *n = NULL;

    plan (NO_PLAN);

    if (!(n = rnode_create ("foo", 0, "0-3")))
        BAIL_OUT ("could not create an rnode object");
    is (n->hostname, "foo",
        "rnode is has hostname set");
    ok (n->up,
        "rnode is created in up state by default");
    n->up = false;
    ok (rnode_avail (n) == 0,
        "rnode_avail == 0 for down rnode");

    ok (rnode_alloc (n, 1, &ids) < 0 && errno == EHOSTDOWN,
        "rnode_alloc on down host returns EHOSTDOWN");

    n->up = true;
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

    struct idset *idset = idset_decode ("0-3");
    n = rnode_create_idset ("foo", 3, idset);
    idset_destroy (idset);
    if (n == NULL)
        BAIL_OUT ("rnode_create_idset failed");
    is (n->hostname, "foo",
        "rnode hostname set correctly");
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

    test_diff ();
    test_intersect ();
    test_add_child ();
    test_copy ();
    test_properties ();
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
