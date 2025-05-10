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
#include "ccan/array_size/array_size.h"
#include "ccan/ptrint/ptrint.h"

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

    topo = topology_create (NULL, 16, NULL);
    ok (topo != NULL,
        "topology_create size=16 works");
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

    topo = topology_create ("kary:1", 16, NULL);
    ok (topo != NULL,
        "topology_create kary:1 size=16 works");
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

    topo = topology_create ("kary:2", 16, NULL);
    ok (topo != NULL,
        "topology_create kary:2 size=16 works");
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

    topo = topology_create ("kary:2", 16, NULL);
    ok (topo != NULL,
        "topology_create kary:2 size=16 works");
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

struct internal_ranks_test {
    int size;
    const char *uri;
    const char *expected_ranks;
};

struct internal_ranks_test internal_ranks_tests[] = {
    { 1,  "kary:2",  ""    },
    { 2,  "kary:2",  "0"   },
    { 4,  "kary:2",  "0-1" },
    { 4,  "kary:0",  "0"   },
    { 16, "kary:2",  "0-7" },
    { 48, "kary:2",  "0-23"},
    { 48, "kary:0",  "0"   },
    { 48, "kary:16", "0-2" },
    { 4,  "binomial","0,2" },
    { 8,  "binomial","0,2,4,6" },
    { 16, "binomial","0,2,4,6,8,10,12,14" },
    { -1, NULL, NULL  }
};

void test_internal_ranks (void)
{
    struct topology *topo;
    struct idset *result;
    struct idset *expected;
    char *s;

    struct internal_ranks_test *t = internal_ranks_tests;
    while (t && t->expected_ranks) {
        if (!(topo = topology_create (t->uri, t->size, NULL)))
            BAIL_OUT ("failed to create topology %s size=%d",
                      t->uri,
                      t->size);
        if (!(expected = idset_decode (t->expected_ranks)))
            BAIL_OUT ("failed to decode expected ranks=%d",
                      t->expected_ranks);
        result = topology_get_internal_ranks (topo);
        ok (result != NULL,
            "topology_get_internal_ranks(size=%d, kary=%d) works");
        s = idset_encode (result, IDSET_FLAG_RANGE);

        ok (idset_equal (result, expected),
            "result was %s (expected %s)",
            s,
            t->expected_ranks);

        topology_decref (topo);
        idset_destroy (expected);
        idset_destroy (result);
        free (s);
        t++;
    }
}

struct pmap {
    int parent;
    const char *children;
    struct idset *ids_children;
};
void pmap_clean (struct pmap *map, size_t count)
{
    for (int i = 0; i < count; i++) {
        idset_destroy (map[i].ids_children);
        map[i].ids_children = NULL;
    }
}
int pmap_lookup (struct pmap *map, size_t count, int rank, int *parent_rank)
{
    for (int i = 0; i < count; i++) {
        if (!map[i].ids_children) {
            if (!(map[i].ids_children = idset_decode (map[i].children)))
                BAIL_OUT ("idset_decode failed");
        }
        if (idset_test (map[i].ids_children, rank)) {
            *parent_rank = map[i].parent;
            return 0;
        }
    }
    return -1;
}
json_t *pmap_hosts (struct pmap *map, size_t count, int size)
{
    json_t *hosts;
    if (!(hosts = json_array ()))
        return NULL;
    for (int rank = 0; rank < size; rank++) {
        char host[16];
        char phost[16];
        json_t *entry;
        int parent_rank;

        snprintf (host, sizeof (host), "test%d", rank);
        if (pmap_lookup (map, count, rank, &parent_rank) < 0)
            entry = json_pack ("{s:s}", "host", host);
        else {
            snprintf (phost, sizeof (phost), "test%d", parent_rank);
            entry = json_pack ("{s:s s:s}", "host", host, "parent", phost);
        }
        if (!entry || json_array_append_new (hosts, entry) < 0)
            BAIL_OUT ("failed to append hosts array entry");
    }
    return hosts;
}

/* Does topology have 'expected' (idset) internal ranks?
 */
bool check_internal (struct topology *topo, const char *expected)
{
    struct idset *exp;
    struct idset *out;
    bool result;

    if (!(exp = idset_decode (expected)))
        BAIL_OUT ("idset_decode failed");
    if (!(out = topology_get_internal_ranks (topo)))
        BAIL_OUT ("topology_get_internal_ranks failed");

    result = idset_equal (out, exp);

    char *s = idset_encode (out, IDSET_FLAG_RANGE);
    diag ("%s %s %s", s, result ? "==" : "!=", expected);
    free (s);

    idset_destroy (out);
    idset_destroy (exp);

    return result;
}

struct pmap cust1[] = {
    { .parent = 0, .children = "1,2,64,128,192" },
    { .parent = 1, .children = "3-63" },
    { .parent = 64, .children = "65-127" },
    { .parent = 128, .children = "129-191" },
    { .parent = 192, .children = "193-255" },
};
struct pmap bad1[] = {
    { .parent = 1, .children = "0" }, // 0 can't have a parent
};
struct pmap bad2[] = {
    { .parent = 1, .children = "2" },
    { .parent = 2, .children = "3" },
    { .parent = 3, .children = "1" }, // cycle
};
struct pmap bad3[] = {
    { .parent = 1, .children = "1" }, // small cycle!
};

void test_custom (void)
{
    struct topology *topo;
    flux_error_t error;
    json_t *hosts;

    topo = topology_create ("custom:zzz", 256, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "topology_create custom: fails with URI path");

    topology_hosts_set (NULL);
    topo = topology_create ("custom:", 256, &error);
    ok (topo != NULL,
        "topology_create custom: works without hosts array");
    topology_decref (topo);

    hosts = pmap_hosts (bad1, ARRAY_SIZE (bad1), 2);
    topology_hosts_set (hosts);
    topo = topology_create ("custom:", 2, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "topology_create custom failed with rank 0 parent");
    topology_hosts_set (NULL);
    json_decref (hosts);

    hosts = pmap_hosts (bad2, ARRAY_SIZE (bad2), 16);
    topology_hosts_set (hosts);
    topo = topology_create ("custom:", 16, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "topology_create custom failed with graph cycle");
    topology_hosts_set (NULL);
    json_decref (hosts);

    hosts = pmap_hosts (bad3, ARRAY_SIZE (bad3), 16);
    topology_hosts_set (hosts);
    topo = topology_create ("custom:", 16, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "topology_create custom failed with self as parent");
    topology_hosts_set (NULL);
    json_decref (hosts);

    hosts = pmap_hosts (cust1, ARRAY_SIZE (cust1), 256);
    topology_hosts_set (hosts);

    topo = topology_create ("custom:", 2, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "topology_create custom failed with mismatched topo and host size");

    topo = topology_create ("custom:", 256, &error);
    topology_hosts_set (NULL);
    ok (topo != NULL,
        "configured custom 256 node topo");
    ok (topology_set_rank (topo, 1) == 0,
        "set rank to 1");
    ok (topology_get_parent (topo) == 0,
        "parent is 0");
    ok (topology_get_level (topo) == 1,
        "level is 1");
    ok (topology_get_descendant_count (topo) == 61,
        "descendant_count is 61");
    ok (check_internal (topo, "0-1,64,128,192"),
        "topology has expected internal ranks");
    topology_decref (topo);
    json_decref (hosts);
}

void test_invalid (void)
{
    struct topology *topo;
    int a[16];

    if (!(topo = topology_create (NULL, 16, NULL)))
        BAIL_OUT ("could not create topology");

    errno = 0;
    ok (topology_create (NULL, 0, NULL) == NULL && errno == EINVAL,
        "topology_create size=0 fails with EINVAL");

    lives_ok ({topology_decref (NULL);},
             "topology_decref topo=NULL doesn't crash");

    ok (topology_incref (NULL) == NULL,
        "topology_incref topo=NULL returns NULL");

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
    ok (topology_rank_aux_get (NULL, 0, "foo") == NULL && errno == EINVAL,
        "topology_rank_aux_get topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_rank_aux_get (topo, -1, "foo") == NULL && errno == EINVAL,
        "topology_rank_aux_get rank=-1 fails with EINVAL");
    errno = 0;
    ok (topology_rank_aux_get (topo, 99, "foo") == NULL && errno == EINVAL,
        "topology_rank_aux_get rank=99 fails with EINVAL");
    errno = 0;
    ok (topology_rank_aux_get (topo, 0, "foo") == NULL && errno == ENOENT,
        "topology_rank_aux_get key=unknown fails with ENOENT");

    errno = 0;
    ok (topology_rank_aux_set (NULL, 0, "foo", "bar", NULL) < 0
        && errno == EINVAL,
        "topology_rank_aux_set topo=NULL fails with EINVAL");
    errno = 0;
    ok (topology_rank_aux_set (topo, -1, "foo", "bar", NULL) < 0
        && errno == EINVAL,
        "topology_rank_aux_set rank=-1 fails with EINVAL");
    errno = 0;
    ok (topology_rank_aux_set (topo, 99, "foo", "bar", NULL) < 0
        && errno == EINVAL,
        "topology_rank_aux_set rank=99 fails with EINVAL");

    errno = 0;
    ok (topology_get_internal_ranks (NULL) == NULL && errno == EINVAL,
        "topolog_get_internal_ranks (NULL) returns EINVAL");

    topology_decref (topo);
}

void test_rank_aux (void)
{
    struct topology *topo;
    int errors;

    if (!(topo = topology_create (NULL, 16, NULL)))
        BAIL_OUT ("topology_create failed");

    errors = 0;
    for (int i = 0; i < 16; i++) {
        if (topology_rank_aux_set (topo, i, "rank", int2ptr (i + 1), NULL) < 0)
            errors++;
    }
    ok (errors == 0,
        "topology_rank_aux_set works for all ranks");
    errors = 0;
    for (int i = 0; i < 16; i++) {
        void *ptr;
        if (!(ptr = (topology_rank_aux_get (topo, i, "rank")))
            || ptr2int (ptr) != i + 1)
            errors++;
    }
    ok (errors == 0,
        "topology_rank_aux_get returns expected result for all ranks");

    topology_decref (topo);
}

void test_binomial5 (void)
{
    struct topology *topo;
    flux_error_t error;
    int children[16];

    topo = topology_create ("binomial:zz", 5, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "binomial topology fails with unknown path");

    topo = topology_create ("binomial", 5, &error);
    ok (topo != NULL,
        "binomal topology of size=5 (non power of 2) works");

    ok (topology_set_rank (topo, 1) == 0,
        "set rank to 1");
    ok (topology_get_parent (topo) == 0,
        "rank 1 parent is 0");
    ok (topology_get_child_ranks (topo, NULL, 0) == 0,
        "rank 1 has no children");
    ok (topology_get_level (topo) == 1,
        "rank 1 level is 1");

    ok (topology_set_rank (topo, 2) == 0,
        "set rank to 2");
    ok (topology_get_parent (topo) == 0,
        "rank 2 parent is 0");
    ok (topology_get_child_ranks (topo, children, ARRAY_SIZE (children)) == 1,
        "rank 2 has 1 child");
    ok (children[0] == 3,
        "child is rank 3");
    ok (topology_get_level (topo) == 1,
        "rank 2 level is 1");

    ok (topology_set_rank (topo, 3) == 0,
        "set rank to 3");
    ok (topology_get_parent (topo) == 2,
        "rank 3 parent is 2");
    ok (topology_get_child_ranks (topo, NULL, 0) == 0,
        "rank 3 has no children");
    ok (topology_get_level (topo) == 2,
        "rank 3 level is 2");

    ok (topology_set_rank (topo, 4) == 0,
        "set rank to 4");
    ok (topology_get_parent (topo) == 0,
        "rank 4 parent is 0");
    ok (topology_get_child_ranks (topo, NULL, 0) == 0,
        "rank 4 has no children");
    ok (topology_get_level (topo) == 1,
        "rank 4 level is 2");

    topology_decref (topo);
}

void test_mincrit5 (void)
{
    struct topology *topo;
    flux_error_t error;

    topo = topology_create ("mincrit:zz", 5, &error);
    if (!topo)
        diag ("%s", error.text);
    ok (topo == NULL,
        "mincrit topology fails with unknown path");

    topo = topology_create ("mincrit:2", 5, &error);
    ok (topo != NULL,
        "mincrit:2 topology of size=5 works");

    ok (topology_set_rank (topo, 1) == 0,
        "set rank to 1");
    ok (topology_get_parent (topo) == 0,
        "rank 1 parent is 0");
    ok (topology_get_child_ranks (topo, NULL, 0) == 1,
        "rank 1 has one child");
    ok (topology_get_level (topo) == 1,
        "rank 1 level is 1");

    ok (topology_set_rank (topo, 2) == 0,
        "set rank to 2");
    ok (topology_get_parent (topo) == 0,
        "rank 2 parent is 0");
    ok (topology_get_child_ranks (topo, NULL, 0) == 1,
        "rank 2 has one child");
    ok (topology_get_level (topo) == 1,
        "rank 2 level is 1");

    ok (topology_set_rank (topo, 3) == 0,
        "set rank to 3");
    ok (topology_get_parent (topo) == 1,
        "rank 3 parent is 1");
    ok (topology_get_child_ranks (topo, NULL, 0) == 0,
        "rank 3 has no children");
    ok (topology_get_level (topo) == 2,
        "rank 3 level is 2");

    ok (topology_set_rank (topo, 4) == 0,
        "set rank to 4");
    ok (topology_get_parent (topo) == 2,
        "rank 4 parent is 2");
    ok (topology_get_child_ranks (topo, NULL, 0) == 0,
        "rank 4 has no children");
    ok (topology_get_level (topo) == 2,
        "rank 4 level is 2");

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
    test_internal_ranks ();
    test_custom ();
    test_rank_aux ();
    test_binomial5 ();
    test_mincrit5 ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
