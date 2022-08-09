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

#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "src/broker/topology.h"

void check_subtree (json_t *o,
                    const char *s,
                    int exp_rank,
                    int exp_size,
                    size_t exp_count)
{
    int rank = -1;
    int size = -1;
    json_t *children = NULL;
    int rc = -1;

    if (o) {
        rc = json_unpack (o,
                          "{s:i s:i s:o}",
                          "rank", &rank,
                          "size", &size,
                          "children", &children);
    }

    diag ("rank=%d size=%d children=%zu",
          rank,
          size,
          children ? json_array_size (children) : -1);

    ok (rc == 0
        && rank == exp_rank
        && size == exp_size
        && children != NULL
        && json_array_size (children) == exp_count,
        "topology_get_json_subtree_at %s returns expected object", s);
}

void test_flat (void)
{
    struct topology *topo;
    int child_ranks[15];
    json_t *o;
    bool pass;

    topo = topology_create (16);
    ok (topo != NULL,
        "topology_create size=16 works");
    ok (topology_set_kary (topo, 0) == 0,
        "topology_set_kary k=0 allowed to indicate flat topo");
    ok (topology_get_size (topo) == 16,
        "topology_get_size returns 16");
    ok (topology_get_rank (topo) == 0,
        "topology_get_rank returns 0");
    ok (topology_get_parent (topo) < 0,
        "topology_get_parent fails");
    ok (topology_get_child_ranks (topo, child_ranks, 15) == 15,
        "topology_get_child_ranks returns 15");

    pass = true;
    for (int i = 0; i < 15; i++) {
        if (child_ranks[i] != i + 1)
            pass = false;
    }

    ok (pass == true,
        "child_ranks array contains ranks 1-15");
    ok (topology_get_level (topo) == 0,
        "topology_get_level returns 0");
    ok (topology_get_maxlevel (topo) == 1,
        "topology_get_maxlevel returns 1");
    ok (topology_get_descendant_count (topo) == 15,
        "topology_get_descendant_count returns 15");
    ok (topology_get_child_route (topo, 5) == 5,
        "topology_get_child_route rank=5 returns 5");

    o = topology_get_json_subtree_at (topo, 0);
    check_subtree (o, "rank=0", 0, 16, 15);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 15);
    check_subtree (o, "rank=15", 15, 1, 0);
    json_decref (o);

    ok (topology_incref (topo) == topo,
        "topology_incref returns topo pointer");
    topology_decref (topo);
    topology_decref (topo);
}

void test_k1 (void)
{
    struct topology *topo;
    int child_ranks[15];
    json_t *o;

    topo = topology_create (16);
    ok (topo != NULL,
        "topology_create size=16 works");
    ok (topology_set_kary (topo, 1) == 0,
        "topology_set_kary k=1 works");
    ok (topology_get_rank (topo) == 0,
        "topology_get_rank returns 0");
    ok (topology_get_size (topo) == 16,
        "topology_get_size returns 16");
    ok (topology_get_parent (topo) < 0,
        "topology_get_parent fails");
    ok (topology_get_child_ranks (topo, child_ranks, 15) == 1,
        "topology_get_child_ranks returns 1");

    ok (child_ranks[0] == 1,
        "child_ranks array contains ranks 1");
    ok (topology_get_level (topo) == 0,
        "topology_get_level returns 0");
    ok (topology_get_maxlevel (topo) == 15,
        "topology_get_maxlevel returns 15");
    ok (topology_get_descendant_count (topo) == 15,
        "topology_get_descendant_count returns 15");
    ok (topology_get_child_route (topo, 5) == 1,
        "topology_get_child_route rank=5 returns 1");

    o = topology_get_json_subtree_at (topo, 0);
    check_subtree (o, "rank=0", 0, 16, 1);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 1);
    check_subtree (o, "rank=1", 1, 15, 1);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 15);
    check_subtree (o, "rank=15", 15, 1, 0);
    json_decref (o);

    topology_decref (topo);
}

void test_k2 (void)
{
    struct topology *topo;
    int child_ranks[15];
    json_t *o;

    topo = topology_create (16);
    ok (topo != NULL,
        "topology_create size=16 works");
    ok (topology_set_kary (topo, 2) == 0,
        "topology_set_kary k=2 works");
    ok (topology_get_rank (topo) == 0,
        "topology_get_rank returns 0");
    ok (topology_get_size (topo) == 16,
        "topology_get_size returns 16");
    ok (topology_get_parent (topo) < 0,
        "topology_get_parent fails");
    ok (topology_get_child_ranks (topo, child_ranks, 15) == 2,
        "topology_get_child_ranks returns 2");

    ok (child_ranks[0] == 1 && child_ranks[1] == 2,
        "child_ranks array contains ranks 1-2");
    ok (topology_get_level (topo) == 0,
        "topology_get_level returns 0");
    ok (topology_get_maxlevel (topo) == 4,
        "topology_get_maxlevel returns 4");
    ok (topology_get_descendant_count (topo) == 15,
        "topology_get_descendant_count returns 15");
    ok (topology_get_child_route (topo, 5) == 2,
        "topology_get_child_route rank=5 returns 2");

    o = topology_get_json_subtree_at (topo, 0);
    check_subtree (o, "rank=0", 0, 16, 2);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 1);
    check_subtree (o, "rank=1", 1, 8, 2);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 2);
    check_subtree (o, "rank=2", 2, 7, 2);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 3);
    check_subtree (o, "rank=3", 3, 4, 2);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 4);
    check_subtree (o, "rank=4", 4, 3, 2);
    json_decref (o);
    o = topology_get_json_subtree_at (topo, 15);
    check_subtree (o, "rank=15", 15, 1, 0);
    json_decref (o);

    topology_decref (topo);
}

void test_k2_router (void)
{
    struct topology *topo;
    int child_ranks[15];
    json_t *o;

    topo = topology_create (16);
    ok (topo != NULL,
        "topology_create size=16 works");
    ok (topology_set_kary (topo, 2) == 0,
        "topology_set_kary k=2 works");
    ok (topology_set_rank (topo, 1) == 0,
        "topology_set_rank 1 works");
    ok (topology_get_rank (topo) == 1,
        "topology_get_rank returns 1");
    ok (topology_get_size (topo) == 16,
        "topology_get_size returns 16");
    ok (topology_get_parent (topo) == 0,
        "topology_get_parent returns 0");
    ok (topology_get_child_ranks (topo, child_ranks, 15) == 2,
        "topology_get_child_ranks returns 2");
    ok (child_ranks[0] == 3 && child_ranks[1] == 4,
        "child_ranks array contains ranks 3-4");
    ok (topology_get_level (topo) == 1,
        "topology_get_level returns 1");
    ok (topology_get_maxlevel (topo) == 4,
        "topology_get_maxlevel returns 4");
    ok (topology_get_descendant_count (topo) == 7,
        "topology_get_descendant_count returns 7");
    ok (topology_get_child_route (topo, 10) == 4,
        "topology_get_child_route rank=10 returns 4");

    o = topology_get_json_subtree_at (topo, 1);
    check_subtree (o, "rank=1", 1, 8, 2);
    json_decref (o);

    topology_decref (topo);
}

void test_invalid (void)
{
    struct topology *topo;
    int a[16];

    if (!(topo = topology_create (16)))
        BAIL_OUT ("could not create topology");

    errno = 0;
    ok (topology_create (0) == NULL && errno == EINVAL,
        "topology_create size=0 fails with EINVAL");

    lives_ok ({topology_decref (NULL);},
             "topology_decref topo=NULL doesn't crash");

    ok (topology_incref (NULL) == NULL,
        "topology_incref topo=NULL returns NULL");

    errno = 0;
    ok (topology_set_kary (NULL, 2) < 0 && errno == EINVAL,
        "topology_set_kary topo=NULL fails with EINVAL");

    errno = 0;
    ok (topology_set_rank (NULL, 0) < 0 && errno == EINVAL,
        "topology_set_rank topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_set_rank (topo, -1) < 0 && errno == EINVAL,
        "topology_set_rank rank=-1 fails with EINVAL");

    ok (topology_get_rank (NULL) == -1,
        "topology_get_rank topo=NULL returns -1");
    ok (topology_get_size (NULL) == -1,
        "topology_get_rank topo=NULL returns -1");
    ok (topology_get_parent (NULL) == -1,
        "topology_get_parent topo=NULL returns -1");
    ok (topology_get_level (NULL) == 0,
        "topology_get_level topo=NULL returns 0");
    ok (topology_get_maxlevel (NULL) == 0,
        "topology_get_maxlevel topo=NULL returns 0");

    errno = 0;
    ok (topology_get_child_ranks (NULL, NULL, 0) == -1 && errno == EINVAL,
        "topology_get_child_ranks topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_get_child_ranks (topo, NULL, 2) == -1 && errno == EINVAL,
        "topology_get_child_ranks buf=NULL size>0 fails with EINVAL");
    errno = 0;
    ok (topology_get_child_ranks (topo, a, 2) == -1 && errno == EOVERFLOW,
        "topology_get_child_ranks size=too short fails with EOVERFLOW");

    ok (topology_get_descendant_count (NULL) == 0,
        "topology_get_descendant_count topo=NULL returns 0");

    ok (topology_get_child_route (NULL, 1) == -1,
        "topology_get_child_route topo=NULL returns -1");
    ok (topology_get_child_route (topo, 0) == -1,
        "topology_get_child_route rank=0 returns -1");
    ok (topology_get_child_route (topo, 99) == -1,
        "topology_get_child_route rank=99 returns -1");

    errno = 0;
    ok (topology_get_json_subtree_at (NULL, 0) == NULL && errno == EINVAL,
        "topology_get_json_subtree_at topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_get_json_subtree_at (topo, -1) == NULL && errno == EINVAL,
        "topology_get_json_subtree_at rank=-1 fails with EINVAL");

    errno = 0;
    ok (topology_aux_get (NULL, 0, "foo") == NULL && errno == EINVAL,
        "topology_aux_get topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_aux_get (topo, -1, "foo") == NULL && errno == EINVAL,
        "topology_aux_get rank=-1 fails with EINVAL");
    errno = 0;
    ok (topology_aux_get (topo, 99, "foo") == NULL && errno == EINVAL,
        "topology_aux_get rank=99 fails with EINVAL");
    errno = 0;
    ok (topology_aux_get (topo, 0, "foo") == NULL && errno == ENOENT,
        "topology_aux_get key=unknown fails with ENOENT");

    topology_decref (topo);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_flat ();
    test_k1 ();
    test_k2 ();
    test_k2_router ();
    test_invalid ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
