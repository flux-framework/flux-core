/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "rlist.h"

struct testalloc {
    int nnodes;
    int nslots;
    int slot_size;
};

struct rlist_test_entry {
    const char *description;
    const char *mode;
    const char *down;
    struct testalloc alloc;
    const char *result;
    const char *allocated;
    const char *avail;
    int expected_errno;
    bool free;
};

#define RLIST_TEST_END { NULL, NULL, NULL, \
                         { 0, 0, 0 },      \
                         NULL, NULL, NULL, \
                         0, false }

struct rlist_test_entry test_2n_4c[] = {
    { "too large of slot returns EOVERFLOW", NULL, NULL,
      { 0, 1, 5 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EOVERFLOW, false },
    { "too many slots returns error", NULL, NULL,
      { 0, 9, 1 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EOVERFLOW, false },
    { "invalid number of nodes returns error", NULL, NULL,
      { -1, 1, 1 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "invalid number of slots return error", NULL, NULL,
      { 0, 0, 1 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "invalid slot size returns error", NULL, NULL,
      { 0, 1, -1},
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "allocate with all nodes down returns ENOSPC", NULL, "0-1",
      { 0, 1, 1},
      NULL,
      "",
      "",
      ENOSPC, false },
    { "allocating a single core gets expected result", NULL, NULL,
      { 0, 1, 1 },
      "rank0/core0",
      "rank0/core0",
      "rank0/core[1-3] rank1/core[0-3]",
      0, true },
    { "allocating a single core with down rank", NULL, "0",
      { 0, 1, 1 },
      "rank1/core0",
      "rank1/core0",
      "rank1/core[1-3]",
      0, false },
    { "allocating another core (all ranks up)", NULL, NULL,
      { 0, 1, 1 },
      "rank0/core0",
      "rank[0-1]/core0",
      "rank[0-1]/core[1-3]",
      0, false },
    { "allocating another core gets expected result", NULL, NULL,
      { 0, 1, 1 },
      "rank0/core1",
      "rank0/core[0-1] rank1/core0",
      "rank0/core[2-3] rank1/core[1-3]",
      0, false },
    { "allocate 1 slot of size 3 lands on correct node", NULL, NULL,
      { 0, 1, 3 },
      "rank1/core[1-3]",
      "rank0/core[0-1] rank1/core[0-3]",
      "rank0/core[2-3]",
      0, false },
    { "allocate 4 slots of 1 core now returns ENOSPC", NULL, NULL,
      { 0, 4, 1 },
      NULL,
      "rank0/core[0-1] rank1/core[0-3]",
      "rank0/core[2-3]",
      ENOSPC, false },
    { "allocate remaining 2 cores", NULL, NULL,
      { 0, 1, 2 },
      "rank0/core[2-3]",
      "rank[0-1]/core[0-3]",
      "",
       0, false },
    RLIST_TEST_END,
};

struct rlist_test_entry test_6n_4c[] = {
    { "best-fit: alloc 1 core", "best-fit", NULL,
      { 0, 1, 1 },
      "rank0/core0",
      "rank0/core0",
      "rank0/core[1-3] rank[1-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 1 slot/size 3 fits on rank0", "best-fit", NULL,
      { 0, 1, 3 },
      "rank0/core[1-3]",
      "rank0/core[0-3]",
      "rank[1-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 2 slots/size 2 fits on rank1","best-fit", NULL,
      { 0, 2, 2 },
      "rank1/core[0-3]",
      "rank[0-1]/core[0-3]",
      "rank[2-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 3 slot of size 1",            "best-fit", NULL,
      { 0, 3, 1 },
      "rank2/core[0-2]",
      "rank[0-1]/core[0-3] rank2/core[0-2]",
      "rank2/core3 rank[3-5]/core[0-3]",
      0, false },
    { "best-fit alloc 3 slots of 1 core",            "best-fit", NULL,
      { 0, 3, 1 },
      "rank2/core3 rank3/core[0-1]",
      "rank[0-2]/core[0-3] rank3/core[0-1]",
      "rank3/core[2-3] rank[4-5]/core[0-3]",
      0, false },
    RLIST_TEST_END,
};

struct rlist_test_entry test_1024n_4c[] = {
    { "large: 512 nodes with 2 cores", NULL, NULL,
      { 512, 512, 2 },
      "rank[0-511]/core[0-1]",
      "rank[0-511]/core[0-1]",
      "rank[0-511]/core[2-3] rank[512-1023]/core[0-3]",
      0, false
    },
    { "large: 512 slots of 4 cores", NULL, NULL,
      { 0, 512, 4 },
      "rank[512-1023]/core[0-3]",
      "rank[0-511]/core[0-1] rank[512-1023]/core[0-3]",
      "rank[0-511]/core[2-3]",
      0, true
    },
    { "large: 1 core on 10 nodes", NULL, NULL,
      { 10, 10, 1 },
      "rank[512-521]/core0",
      "rank[0-511]/core[0-1] rank[512-521]/core0",
      "rank[0-511]/core[2-3] rank[512-521]/core[1-3] rank[522-1023]/core[0-3]",
      0, false },
    { "large: alloc 2 cores on 128 nodes with free", NULL, NULL,
      { 128, 256, 1 },
      "rank[522-649]/core[0-1]",
      "rank[0-511,522-649]/core[0-1] rank[512-521]/core0",
      "rank[0-511,522-649]/core[2-3] rank[512-521]/core[1-3] rank[650-1023]/core[0-3]",
      0, true
    },
    RLIST_TEST_END,
};


char *R_create (int ranks, int cores)
{
    char *retval;
    char corelist[64];
    char ranklist[64];
    json_t *o = NULL;
    json_t *R_lite = NULL;

    if ((snprintf (corelist, sizeof (corelist)-1, "0-%d", cores-1) < 0)
        || (snprintf (ranklist, sizeof (ranklist)-1, "0-%d", ranks -1) < 0))
        goto err;

    if (!(R_lite = json_pack ("{s:s,s:{s:s}}",
                    "rank", ranklist,
                    "children", "core", corelist)))
        goto err;
    if (!(o = json_pack ("{s:i, s:{s:[O]}}",
                   "version", 1,
                   "execution", "R_lite", R_lite)))
        goto err;
    retval = json_dumps (o, JSON_COMPACT);
    json_decref (o);
    json_decref (R_lite);
    return (retval);
err:
    json_decref (o);
    json_decref (R_lite);
    return NULL;
}

static struct rlist * rlist_testalloc (struct rlist *rl,
                                       struct rlist_test_entry *e)
{
    return rlist_alloc (rl, e->mode,
                        e->alloc.nnodes,
                        e->alloc.nslots,
                        e->alloc.slot_size);
}

static char * rlist_tostring (struct rlist *rl, bool allocated)
{
    char *result;
    struct rlist *l = NULL;
    char *s = NULL;
    json_t *R = NULL;

    if (allocated) {
        struct rlist *alloc = rlist_copy_allocated (rl);
        if (!alloc)
            BAIL_OUT ("rlist_copy_allocated failed! %s", strerror (errno));
        R = rlist_to_R (alloc);
        rlist_destroy (alloc);
    }
    else
        R = rlist_to_R (rl);

    if (!R || !(s = json_dumps (R, JSON_COMPACT)))
        BAIL_OUT ("rlist_to_R* failed!");
    if (!(l = rlist_from_R (s)))
        BAIL_OUT ("rlist_from_R failed!");

    result = rlist_dumps (l);
    rlist_destroy (l);
    free (s);
    json_decref (R);
    return result;
}

void run_test_entries (struct rlist_test_entry tests[], int ranks, int cores)
{
    struct rlist *rl = NULL;
    struct rlist *alloc = NULL;
    struct rlist_test_entry *e = NULL;
    char *R = R_create (ranks, cores);
    if (R == NULL)
        BAIL_OUT ("R_create (ranks=%d, cores=%d) failed", ranks, cores);
    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R (%s)", R);
    free (R);

    e = &tests[0];
    while (e && e->description) {
        int avail_start = rl->avail;

        if (e->down)
            ok (rlist_mark_down (rl, e->down) == 0,
                "marking ranks %s down", e->down);

        alloc = rlist_testalloc (rl, e);
        if (e->result == NULL) {  // Test for expected failure
            ok (alloc == NULL && errno == e->expected_errno,
                "%s: errno=%d", e->description, errno);
        }
        else {
            if (alloc) {
                char *result = rlist_dumps (alloc);
                is (result, e->result, "%s: %s", e->description, result);
                if (e->allocated) {
                    char *s = rlist_tostring (rl, true);
                    is (s, e->allocated, "%s: alloc: %s", e->description, s);
                    free (s);
                }
                if (e->avail) {
                    char *s = rlist_tostring (rl, false);
                    is (s, e->avail, "%s: avail: %s", e->description, s);
                    free (s);
                }
                if (e->free) {
                    ok (rlist_free (rl, alloc) == 0, "rlist_free (%s)", result);
                    ok (avail_start == rl->avail, "freed all cores");
                }
                free (result);
                rlist_destroy (alloc);
            }
            else {
                fail ("%s: %s", e->description, strerror (errno));
            }
        }

        if (e->down)
            ok (rlist_mark_up (rl, e->down) == 0,
                "marking ranks %s back up", e->down);

        char *s = rlist_dumps (rl);
        // diag ("avail=%s", s);
        free (s);
        e++;
    }
    rlist_destroy (rl);
}

static void test_simple (void)
{
    struct rlist *rl = NULL;
    struct rlist *alloc = NULL;
    struct rlist *copy = NULL;

    if (!(rl = rlist_create ()))
        BAIL_OUT ("Failed to create rlist");

    ok (rl->total == 0 && rl->avail == 0,
        "rlist_create creates empty list");
    ok (rlist_append_rank (rl, 0, "0-3") == 0,
        "rlist_append_rank 0, 0-3");
    ok (rl->total == 4 && rl->avail == 4,
        "rlist: avail and total == 4");
    ok (rlist_append_rank (rl, 1, "0-3") == 0,
        "rlist_append_rank 1, 0-3");
    ok (rl->total == 8 && rl->avail == 8,
        "rlist: avail and total == 4");
    ok ((alloc = rlist_alloc (rl, NULL, 0, 8, 1)) != NULL,
        "rlist: alloc all cores works");
    ok (alloc->total == 8 && alloc->avail == 8,
        "rlist: alloc: avail = 8, total == 8");
    ok (rl->total == 8 && rl->avail == 0,
        "rlist: avail == 0, total == 8");
    ok ((copy = rlist_copy_empty (rl)) != NULL,
        "rlist: rlist_copy_empty");
    ok (copy->total == 8 && copy->avail == 8,
        "rlist: copy: total = %d, avail = %d", copy->total, copy->avail);

    rlist_destroy (rl);
    rlist_destroy (alloc);
    rlist_destroy (copy);
}

const char by_rank_issue2202[] = "{\
  \"0\": {\
    \"Package\": 1,\
    \"Core\": 1,\
    \"PU\": 1,\
    \"cpuset\": \"0\"\
  },\
  \"1\": {\
    \"Package\": 1,\
    \"Core\": 1,\
    \"PU\": 1,\
    \"cpuset\": \"1\"\
  },\
  \"2\": {\
    \"Package\": 1,\
    \"Core\": 1,\
    \"PU\": 1,\
    \"cpuset\": \"2\"\
  },\
  \"3\": {\
    \"Package\": 1,\
    \"Core\": 1,\
    \"PU\": 1,\
    \"cpuset\": \"3\"\
  }\
}";

const char by_rank_issue2202b[] = "{\
\"0\": {\
    \"Package\": 1,\
    \"Core\": 2,\
    \"PU\": 2,\
    \"cpuset\": \"0-1\"\
  },\
  \"1\": {\
    \"Package\": 1,\
    \"Core\": 2,\
    \"PU\": 2,\
    \"cpuset\": \"0,2\"\
  },\
  \"2\": {\
    \"Package\": 1,\
    \"Core\": 2,\
    \"PU\": 2,\
    \"cpuset\": \"0,3\"\
  },\
  \"3\": {\
    \"Package\": 1,\
    \"Core\": 2,\
    \"PU\": 2,\
    \"cpuset\": \"3-4\"\
  }\
}";


static void test_issue2202 (void)
{
    char *result = NULL;
    struct rlist *a = NULL;

    struct rlist *rl = rlist_from_hwloc_by_rank (by_rank_issue2202);
    ok (rl != NULL, "issue2202: rlist_from_by_rank");
    if (!rl)
        BAIL_OUT ("unable to create rlist from by_rank_issue2202");

    result = rlist_dumps (rl);
    is (result,
        "rank0/core0 rank1/core1 rank2/core2 rank3/core3",
        "issue2202: rlist_dumps works");
    free (result);

    a = rlist_alloc (rl, "best-fit", 1, 1, 1);
    ok (a != NULL,
        "issue2202: rlist_alloc worked");
    if (a) {
        result = rlist_dumps (a);
        is (result, "rank0/core0", "issue2202: allocated %s", result);
        free (result);
        result = rlist_dumps (rl);
        is (result,
            "rank1/core1 rank2/core2 rank3/core3",
            "issue2202: remaining: %s", result);
        free (result);
        ok (rlist_free (rl, a) == 0,
            "issue2202: rlist_free worked: %s", strerror (errno));
        result = rlist_dumps (rl);
        is (result,
            "rank0/core0 rank1/core1 rank2/core2 rank3/core3",
            "issue2202: rlist now has all cores again");
        free (result);
        rlist_destroy (a);
    }
    rlist_destroy (rl);


    /*  Part B:  test with multiple cores per rank, same cpuset size
     */
    rl = rlist_from_hwloc_by_rank (by_rank_issue2202b);
    ok (rl != NULL, "issue2202: rlist_from_hwloc_by_rank");
    if (!rl)
        BAIL_OUT ("unable to create rlist from by_rank_issue2202b");

    result = rlist_dumps (rl);
    is (result,
        "rank0/core[0-1] rank1/core[0,2] rank2/core[0,3] rank3/core[3-4]",
        "issue2202b: rlist_dumps works");
    free (result);

    a = rlist_alloc (rl, "best-fit", 1, 1, 1);
    ok (a != NULL,
        "issue2202b: rlist_alloc worked");
    if (a) {
        result = rlist_dumps (a);
        is (result, "rank0/core0", "issue2202b: allocated %s", result);
        free (result);
        result = rlist_dumps (rl);
        is (result,
            "rank0/core1 rank1/core[0,2] rank2/core[0,3] rank3/core[3-4]",
            "issue2202b: remaining: %s", result);
        free (result);
        ok (rlist_free (rl, a) == 0,
            "issue2202b: rlist_free worked: %s", strerror (errno));
        result = rlist_dumps (rl);
        is (result,
            "rank0/core[0-1] rank1/core[0,2] rank2/core[0,3] rank3/core[3-4]",
            "issue2202b: rlist now has all cores again");
        free (result);
        rlist_destroy (a);
    }
    rlist_destroy (rl);
}

const char by_rank_issue2473[] = "{\
\"0\": {\
    \"Package\": 1,\
    \"Core\": 4,\
    \"PU\": 4,\
    \"cpuset\": \"0-3\"\
  },\
\"1-2\": {\
    \"Package\": 1,\
    \"Core\": 2,\
    \"PU\": 2,\
    \"cpuset\": \"0-1\"\
  }\
}";

static void test_issue2473 (void)
{
    char *result;
    struct rlist *rl;
    struct rlist *a, *a2;

    rl = rlist_from_hwloc_by_rank (by_rank_issue2473);
    ok (rl != NULL, "issue2473: add_hwloc_by_rank");
    if (rl == NULL)
        BAIL_OUT ("unable to create rlist from by_rank_issue2473");

    ok (rlist_nnodes (rl) == 3,
        "issue2473: created rlist with 3 nodes");
    result = rlist_dumps (rl);
    is (result,
        "rank0/core[0-3] rank[1-2]/core[0-1]",
        "issue2473: rlist_dumps works");
    free (result);

    /* problem: allocated 3 cores on one node */
    a = rlist_alloc (rl, "worst-fit", 3, 3, 1);
    ok (a != NULL,
        "issue2473: rlist_alloc nnodes=3 slots=3 slotsz=1 worked");
    if (!a)
        BAIL_OUT ("rlist_alloc failed");
    ok (rlist_nnodes (a) == 3,
        "issue2473: allocation has 3 nodes");

    result = rlist_dumps (a);
    is (result,
        "rank[0-2]/core0",
        "issue2473: rlist_dumps shows one core per node");
    free (result);
    rlist_free (rl, a);

    /* problem: unsatisfiable */
    a = rlist_alloc (rl, "worst-fit", 3, 8, 1);
    ok (a != NULL,
        "issue2473: rlist_alloc nnodes=3 slots=8 slotsz=1 worked");
    if (a) {
        rlist_free (rl, a);
        rlist_destroy (a);
    }

    /* not a problem but verify slightly counter-intuitive case discussed
     * in the issue:
     * - alloc 1 core on rank0
     * - ask for 2 cores spread across 2 nodes
     * - we should get cores on rank[0-1] not rank[1-2]
     */
    a = rlist_alloc (rl, "worst-fit", 1, 1, 1);
    ok (a != NULL,
        "issue2473: rlist_alloc nnodes=1 slots=1 slotsz=1 worked");
    if (!a)
        BAIL_OUT ("rlist_alloc failed");

    result = rlist_dumps (rl);
    is (result,
        "rank0/core[1-3] rank[1-2]/core[0-1]",
        "issue2473: one core was allocated from rank0");
    free (result);

    a2 = rlist_alloc (rl, "worst-fit", 2, 2, 1);
    ok (a2 != NULL,
        "issue2473: rlist_alloc nnodes=2 slots=2 slotsz=1 worked");
    result = rlist_dumps (a2);
    is (result,
        "rank0/core1 rank1/core0",
        "issue2473: allocated a core from used node, not starting new bin");
    free (result);
    rlist_free (rl, a);
    rlist_destroy (a);
    rlist_free (rl, a2);
    rlist_destroy (a2);

    rlist_destroy (rl);
}

static void test_dumps (void)
{
    char *result = NULL;
    struct rlist *rl = NULL;

    if (!(rl = rlist_create ()))
        BAIL_OUT ("rlist_dumps: failed to create rlist");

    ok (rlist_dumps (NULL) == NULL,
        "rlist_dumps (NULL) == NULL");

    result = rlist_dumps (rl);
    is (result, "",
        "rlist_dumps: empty list returns empty string");
    free (result);

    rlist_append_rank (rl, 0, "0-3");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3]",
        "rlist_dumps with one rank 4 cores gets expected result");
    free (result);

    rlist_append_rank (rl, 1, "0-7");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3] rank1/core[0-7]",
        "rlist_dumps with two ranks gets expected result");
    free (result);

    rlist_append_rank (rl, 1234567, "0-12345");
    rlist_append_rank (rl, 1234568, "0-12346");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3] rank1/core[0-7] "
                "rank1234567/core[0-12345] rank1234568/core[0-12346]",
        "rlist_dumps with long reuslt");
    free (result);
    rlist_destroy (rl);
}

static void test_updown ()
{
    struct rlist *rl = NULL;
    struct rlist *rl2 = NULL;
    char *R = R_create (4, 4);
    rl = rlist_from_R (R);
    if (rl == NULL)
        BAIL_OUT ("rlist_from_R failed");
    free (R);

    ok (rl->avail == 16,
        "rlist avail == 16");
    ok (rlist_mark_down (rl, "all") == 0,
        "rlist_mark_down: all works");
    ok (rl->avail == 0,
        "rlist avail == 0");
    ok (rlist_mark_up (rl, "0-1") == 0,
        "rlist_mark_up (0-1) works");
    ok (rl->avail == 8,
        "rl avail == 8");
    ok (rlist_mark_up (rl, "all") == 0,
        "rlist_mark_up (all) works");
    ok (rl->avail == 16,
        "rl avail == 16");

    rl2 = rlist_alloc (rl, NULL, 0, 4, 1);
    ok (rl2 != NULL,
        "rlist_alloc() works when all nodes up");

    ok (rl->avail == 12,
        "rl avail == 12");

    ok (rlist_mark_down (rl, "all") == 0,
        "rlist_mark_down all with some resources allocated");

    ok (rl->avail == 0,
        "rl avail == 0");

    ok (rlist_free (rl, rl2) == 0,
        "rlist_free original");
    rlist_destroy (rl2);

    ok (rl->avail == 0,
        "rl avail == %d", rl->avail);

    ok (rlist_mark_up (rl, "0-2") == 0,
        "rlist_mark_up all but rank 3 up");

    ok (rlist_alloc (rl, NULL, 4, 4, 1) == NULL && errno == ENOSPC,
        "allocation with 4 nodes fails with ENOSPC");

    ok (rlist_mark_up (rl, "3") == 0,
        "rlist_mark_up 3");
    rl2 = rlist_alloc (rl, NULL, 4, 4, 1);

    ok (rl2 != NULL,
        "rlist_alloc() for 4 nodes now succeeds");

    rlist_destroy (rl);
    rlist_destroy (rl2);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_simple ();
    test_dumps ();
    run_test_entries (test_2n_4c,       2, 4);
    run_test_entries (test_6n_4c,       6, 4);
    run_test_entries (test_1024n_4c, 1024, 4);
    test_issue2202 ();
    test_issue2473 ();
    test_updown ();

    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
