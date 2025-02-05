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
#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "rlist.h"

struct testalloc {
    int nnodes;
    int nslots;
    int slot_size;
    int exclusive;
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
                         { 0, 0, 0, 0 },      \
                         NULL, NULL, NULL, \
                         0, false }

struct rlist_test_entry test_2n_4c[] = {
    { "too large of slot returns EOVERFLOW", NULL, NULL,
      { 0, 1, 5, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EOVERFLOW, false },
    { "too many slots returns error", NULL, NULL,
      { 0, 9, 1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EOVERFLOW, false },
    { "invalid number of nodes returns error", NULL, NULL,
      { -1, 1, 1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "Too many nodes returns error", NULL, NULL,
      { 3, 4, 1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EOVERFLOW, false },
    { "nodes > slots returns error", NULL, NULL,
      { 2, 1, 1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "invalid number of slots return error", NULL, NULL,
      { 0, 0, 1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "invalid slot size returns error", NULL, NULL,
      { 0, 1, -1, 0 },
      NULL,
      "",
      "rank[0-1]/core[0-3]",
      EINVAL, false },
    { "allocate with all nodes down returns ENOSPC", NULL, "0-1",
      { 0, 1, 1, 0 },
      NULL,
      "",
      "",
      ENOSPC, false },
    { "allocating a single core gets expected result", NULL, NULL,
      { 0, 1, 1, 0 },
      "rank0/core0",
      "rank0/core0",
      "rank0/core[1-3] rank1/core[0-3]",
      0, true },
    { "allocating a single core with down rank", NULL, "0",
      { 0, 1, 1, 0 },
      "rank1/core0",
      "rank1/core0",
      "rank1/core[1-3]",
      0, false },
    { "allocating another core (all ranks up)", NULL, NULL,
      { 0, 1, 1, 0 },
      "rank0/core0",
      "rank[0-1]/core0",
      "rank[0-1]/core[1-3]",
      0, false },
    { "allocating another core gets expected result", NULL, NULL,
      { 0, 1, 1, 0 },
      "rank0/core1",
      "rank0/core[0-1] rank1/core0",
      "rank0/core[2-3] rank1/core[1-3]",
      0, false },
    { "allocate 1 slot of size 3 lands on correct node", NULL, NULL,
      { 0, 1, 3, 0 },
      "rank1/core[1-3]",
      "rank0/core[0-1] rank1/core[0-3]",
      "rank0/core[2-3]",
      0, false },
    { "allocate 4 slots of 1 core now returns ENOSPC", NULL, NULL,
      { 0, 4, 1, 0 },
      NULL,
      "rank0/core[0-1] rank1/core[0-3]",
      "rank0/core[2-3]",
      ENOSPC, false },
    { "allocate remaining 2 cores", NULL, NULL,
      { 0, 1, 2, 0 },
      "rank0/core[2-3]",
      "rank[0-1]/core[0-3]",
      "",
       0, false },
    RLIST_TEST_END,
};

struct rlist_test_entry test_6n_4c[] = {
    { "best-fit: alloc 1 core", "best-fit", NULL,
      { 0, 1, 1, 0 },
      "rank0/core0",
      "rank0/core0",
      "rank0/core[1-3] rank[1-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 1 slot/size 3 fits on rank0", "best-fit", NULL,
      { 0, 1, 3, 0 },
      "rank0/core[1-3]",
      "rank0/core[0-3]",
      "rank[1-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 2 slots/size 2 fits on rank1","best-fit", NULL,
      { 0, 2, 2, 0 },
      "rank1/core[0-3]",
      "rank[0-1]/core[0-3]",
      "rank[2-5]/core[0-3]",
      0, false },
    { "best-fit: alloc 3 slot of size 1",            "best-fit", NULL,
      { 0, 3, 1, 0 },
      "rank2/core[0-2]",
      "rank[0-1]/core[0-3] rank2/core[0-2]",
      "rank2/core3 rank[3-5]/core[0-3]",
      0, false },
    { "best-fit alloc 3 slots of 1 core",            "best-fit", NULL,
      { 0, 3, 1, 0 },
      "rank2/core3 rank3/core[0-1]",
      "rank[0-2]/core[0-3] rank3/core[0-1]",
      "rank3/core[2-3] rank[4-5]/core[0-3]",
      0, false },
    RLIST_TEST_END,
};

struct rlist_test_entry test_1024n_4c[] = {
    { "large: 512 nodes with 2 cores", NULL, NULL,
      { 512, 512, 2, 0 },
      "rank[0-511]/core[0-1]",
      "rank[0-511]/core[0-1]",
      "rank[0-511]/core[2-3] rank[512-1023]/core[0-3]",
      0, false
    },
    { "large: 512 slots of 4 cores", NULL, NULL,
      { 0, 512, 4, 0 },
      "rank[512-1023]/core[0-3]",
      "rank[0-511]/core[0-1] rank[512-1023]/core[0-3]",
      "rank[0-511]/core[2-3]",
      0, true
    },
    { "large: 1 core on 10 nodes", NULL, NULL,
      { 10, 10, 1, 0 },
      "rank[512-521]/core0",
      "rank[0-511]/core[0-1] rank[512-521]/core0",
      "rank[0-511]/core[2-3] rank[512-521]/core[1-3] rank[522-1023]/core[0-3]",
      0, false },
    { "large: alloc 2 cores on 128 nodes with free", NULL, NULL,
      { 128, 256, 1, 0 },
      "rank[522-649]/core[0-1]",
      "rank[0-511,522-649]/core[0-1] rank[512-521]/core0",
      "rank[0-511,522-649]/core[2-3] rank[512-521]/core[1-3] rank[650-1023]/core[0-3]",
      0, true
    },
    RLIST_TEST_END,
};


struct rlist_test_entry test_exclusive[] = {
    { "exclusive: exclusive without nnodes fails",
      NULL,
      NULL,
      { 0, 1, 1, 1 },
      NULL,
      "",
      "rank[0-3]/core[0-3]",
      EINVAL,
      false
    },
    { "exclusive: allocate one core first",
      NULL,
      NULL,
      { 0, 1, 1, 0 },
      "rank0/core0",
      "rank0/core0",
      "rank0/core[1-3] rank[1-3]/core[0-3]",
      0,
      false
    },
    { "exclusive: exclusively allocate 2 nodes",
      NULL,
      NULL,
      { 2, 2, 1, 1 },
      "rank[1-2]/core[0-3]",
      "rank0/core0 rank[1-2]/core[0-3]",
      "rank0/core[1-3] rank3/core[0-3]",
      0,
      false
    },
    { "exclusive: exclusively allocate 2 nodes fails",
      NULL,
      NULL,
      { 2, 2, 1, 1 },
      NULL,
      "rank0/core0 rank[1-2]/core[0-3]",
      "rank0/core[1-3] rank3/core[0-3]",
      ENOSPC,
      false
    },
    { "exclusive: but 1 node works",
      NULL,
      NULL,
      { 1, 1, 1, 1 },
      "rank3/core[0-3]",
      "rank0/core0 rank[1-3]/core[0-3]",
      "rank0/core[1-3]",
      0,
      false
    },
    { "exclusive: last 3 cores can be allocated non-exclusively",
      NULL,
      NULL,
      { 0, 3, 1, 0 },
      "rank0/core[1-3]",
      "rank[0-3]/core[0-3]",
      "",
      0,
      false,
    },
    RLIST_TEST_END,
};


char *R_create (const char *ranklist,
                const char *corelist,
                const char *gpus,
                const char *nodelist,
                const char *properties)
{
    char *retval;
    json_t *o = NULL;
    json_t *R_lite = NULL;

    if (gpus) {
        if (!(R_lite = json_pack ("{s:s,s:{s:s s:s}}",
                        "rank", ranklist,
                        "children",
                          "core", corelist ? corelist : "",
                          "gpu", gpus)))
            goto err;
    }
    else {
        if (!(R_lite = json_pack ("{s:s,s:{s:s}}",
                        "rank", ranklist,
                        "children", "core", corelist)))
            goto err;
    }
    if (nodelist) {
        if (!(o = json_pack ("{s:i, s:{s:[s], s:[O]}}",
                   "version", 1,
                   "execution",
                     "nodelist", nodelist,
                     "R_lite", R_lite)))
            goto err;
    }
    else {
        if (!(o = json_pack ("{s:i, s:{s:[O]}}",
                   "version", 1,
                   "execution", "R_lite", R_lite)))
            goto err;
    }
    if (properties) {
        json_error_t error;
        json_t *execution;
        json_t *prop = json_loads (properties, JSON_DECODE_ANY, &error);
        if (!prop)
            BAIL_OUT ("json_loads ('%s'): %s", properties, error.text);
        if (!(execution = json_object_get (o, "execution")))
            goto err;
        json_object_set_new (execution, "properties", prop);
    }
    retval = json_dumps (o, JSON_COMPACT|JSON_ENCODE_ANY);
    json_decref (o);
    json_decref (R_lite);
    return (retval);
err:
    json_decref (o);
    json_decref (R_lite);
    return NULL;
}

static struct rlist * rl_alloc (struct rlist *rl,
                                const char *mode,
                                int nnodes,
                                int nslots,
                                int slot_size,
                                int exclusive)
{
    struct rlist_alloc_info ai = {
        .mode = mode,
        .nnodes = nnodes,
        .nslots = nslots,
        .slot_size = slot_size,
        .exclusive = exclusive
    };
    flux_error_t error;
    struct rlist *result = rlist_alloc (rl, &ai, &error);
    if (!result)
        diag ("rlist_alloc: %s", error.text);
    return result;
}

static struct rlist * rlist_testalloc (struct rlist *rl,
                                       struct rlist_test_entry *e)
{
    return rl_alloc (rl, e->mode,
                     e->alloc.nnodes,
                     e->alloc.nslots,
                     e->alloc.slot_size,
                     e->alloc.exclusive);
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

static char *R_create_num (int ranks, int cores)
{
    char corelist[64];
    char ranklist[64];
    if ((snprintf (corelist, sizeof (corelist)-1, "0-%d", cores-1) < 0)
        || (snprintf (ranklist, sizeof (ranklist)-1, "0-%d", ranks -1) < 0))
        return NULL;
    return R_create (ranklist, corelist, NULL, NULL, NULL);
}

void run_test_entries (struct rlist_test_entry tests[], int ranks, int cores)
{
    struct rlist *rl = NULL;
    struct rlist *alloc = NULL;
    struct rlist_test_entry *e = NULL;
    char *R = R_create_num (ranks, cores);
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
                    diag ("total=%d, avail=%d", rl->total, rl->avail);
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
                fail ("%s: rlist_testalloc: %s",
                      e->description,
                      strerror (errno));
            }
        }

        if (e->down)
            ok (rlist_mark_up (rl, e->down) == 0,
                "marking ranks %s back up", e->down);

        char *s = rlist_dumps (rl);
        diag ("avail=%s", s);
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
    ok (rlist_append_rank_cores (rl, "host", 0, "0-3") == 0,
        "rlist_append_rank_cores 0, 0-3");
    ok (rl->total == 4 && rl->avail == 4,
        "rlist: avail and total == 4");
    ok (rlist_append_rank_cores (rl, "host", 1, "0-3") == 0,
        "rlist_append_rank_cores 1, 0-3");
    ok (rl->total == 8 && rl->avail == 8,
        "rlist: avail and total == 4");
    ok ((alloc = rl_alloc (rl, NULL, 0, 8, 1, 0)) != NULL,
        "rlist: alloc all cores works");
    ok (alloc->total == 8 && alloc->avail == 8,
        "rlist: alloc: got %d/%d (expected 8/8)",
        alloc->avail, alloc->total);
    ok (rl->total == 8 && rl->avail == 0,
        "rlist: avail == 0, total == 8");
    ok ((copy = rlist_copy_empty (rl)) != NULL,
        "rlist: rlist_copy_empty");
    if (!copy)
        BAIL_OUT ("rlist_copy_empty failed!");
    ok (copy->total == 8 && copy->avail == 8,
        "rlist: copy: total = %d, avail = %d", copy->total, copy->avail);

    rlist_destroy (rl);
    rlist_destroy (alloc);
    rlist_destroy (copy);
}

const char R_issue2202[] = "{\n\
  \"version\": 1,\n\
  \"execution\": {\n\
    \"R_lite\": [\n\
      {\n\
        \"rank\": \"0\",\n\
        \"children\": {\n\
          \"core\": \"0\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"1\",\n\
        \"children\": {\n\
          \"core\": \"1\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"2\",\n\
        \"children\": {\n\
          \"core\": \"2\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"3\",\n\
        \"children\": {\n\
          \"core\": \"3\"\n\
        }\n\
      }\n\
    ]\n\
  }\n\
}";

const char R_issue2202b[] = "{\
  \"version\": 1,\n\
  \"execution\": {\n\
    \"R_lite\": [\n\
      {\n\
        \"rank\": \"0\",\n\
        \"children\": {\n\
          \"core\": \"0-1\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"1\",\n\
        \"children\": {\n\
          \"core\": \"0,2\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"2\",\n\
        \"children\": {\n\
          \"core\": \"0,3\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"3\",\n\
        \"children\": {\n\
          \"core\": \"3-4\"\n\
        }\n\
      }\n\
    ]\n\
  }\n\
}";

static void test_issue2202 (void)
{
    char *result = NULL;
    struct rlist *a = NULL;

    struct rlist *rl = rlist_from_R (R_issue2202);
    if (!rl)
        BAIL_OUT ("unable to create rlist from R_issue2202");
    ok (rl != NULL, "issue2202: rlist_from_R");

    result = rlist_dumps (rl);
    is (result,
        "rank0/core0 rank1/core1 rank2/core2 rank3/core3",
        "issue2202: rlist_dumps works");
    free (result);

    a = rl_alloc (rl, "best-fit", 1, 1, 1, 0);
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
    rl = rlist_from_R (R_issue2202b);
    if (!rl)
        BAIL_OUT ("unable to create rlist from R_issue2202b");

    ok (rl != NULL, "issue2202: rlist_from_R");
    result = rlist_dumps (rl);
    is (result,
        "rank0/core[0-1] rank1/core[0,2] rank2/core[0,3] rank3/core[3-4]",
        "issue2202b: rlist_dumps works");
    free (result);

    a = rl_alloc (rl, "best-fit", 1, 1, 1, 0);
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

const char R_issue2473[] = "{\
  \"version\": 1,\n\
  \"execution\": {\n\
    \"R_lite\": [\n\
      {\n\
        \"rank\": \"0\",\n\
        \"children\": {\n\
          \"core\": \"0-3\"\n\
        }\n\
      },\n\
      {\n\
        \"rank\": \"1-2\",\n\
        \"children\": {\n\
          \"core\": \"0-1\"\n\
        }\n\
      }\n\
    ]\n\
  }\n\
}";

static void test_issue2473 (void)
{
    char *result;
    struct rlist *rl;
    struct rlist *a, *a2;

    rl = rlist_from_R (R_issue2473);
    if (rl == NULL)
        BAIL_OUT ("unable to create rlist from R_issue2473");
    ok (rl != NULL, "issue2473: rlist_from_R");

    ok (rlist_nnodes (rl) == 3,
        "issue2473: created rlist with 3 nodes");
    result = rlist_dumps (rl);
    is (result,
        "rank0/core[0-3] rank[1-2]/core[0-1]",
        "issue2473: rlist_dumps works");
    free (result);

    /* problem: allocated 3 cores on one node */
    a = rl_alloc (rl, "worst-fit", 3, 3, 1, 0);
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
    rlist_destroy (a);

    /* problem: unsatisfiable */
    a = rl_alloc (rl, "worst-fit", 3, 8, 1, 0);
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
    a = rl_alloc (rl, "worst-fit", 1, 1, 1, 0);
    ok (a != NULL,
        "issue2473: rlist_alloc nnodes=1 slots=1 slotsz=1 worked");
    if (!a)
        BAIL_OUT ("rlist_alloc failed");

    result = rlist_dumps (rl);
    is (result,
        "rank0/core[1-3] rank[1-2]/core[0-1]",
        "issue2473: one core was allocated from rank0");
    free (result);

    a2 = rl_alloc (rl, "worst-fit", 2, 2, 1, 0);
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

    rlist_append_rank_cores (rl, "host", 0, "0-3");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3]",
        "rlist_dumps with one rank 4 cores gets expected result");
    free (result);

    rlist_append_rank_cores (rl, "host", 1, "0-7");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3] rank1/core[0-7]",
        "rlist_dumps with two ranks gets expected result");
    free (result);

    rlist_append_rank_cores (rl, "host", 1234567, "0-12345");
    rlist_append_rank_cores (rl, "host", 1234568, "0-12346");
    result = rlist_dumps (rl);
    is (result, "rank0/core[0-3] rank1/core[0-7] "
                "rank1234567/core[0-12345] rank1234568/core[0-12346]",
        "rlist_dumps with long result");
    free (result);
    rlist_destroy (rl);
}

static void test_updown ()
{
    struct rlist *rl = NULL;
    struct rlist *rl2 = NULL;
    char *R = R_create ("0-3", "0-3", NULL, "host[0-3]", NULL);
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

    rl2 = rl_alloc (rl, NULL, 0, 4, 1, 0);
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

    ok (rl_alloc (rl, NULL, 4, 4, 1, 0) == NULL && errno == ENOSPC,
        "allocation with 4 nodes fails with ENOSPC");

    ok (rlist_mark_up (rl, "3") == 0,
        "rlist_mark_up 3");
    rl2 = rl_alloc (rl, NULL, 4, 4, 1, 0);

    ok (rl2 != NULL,
        "rlist_alloc() for 4 nodes now succeeds");

    rlist_destroy (rl);
    rlist_destroy (rl2);
}

struct append_test {
    const char *ranksa;
    const char *coresa;
    const char *hostsa;

    const char *ranksb;
    const char *coresb;
    const char *hostsb;

    int total_cores;
    int total_nodes;
    const char *nodelist;
};

struct append_test append_tests[] = {
    {
        "1", "0-3", "foo15",
        "0", "0-3", "foo16",
        8,
        2,
        "foo[16,15]",
    },
    {
        "0,2-3", "0-3", "foo[0,2-3]",
        "1",     "0-3", "foo1",
        16,
        4,
        "foo[0-3]",
    },
    {
        "0", "0-3", "foo0",
        "0", "4-7", "foo0",
        8,
        1,
        "foo0",
    },
    {
        "[0-1023]", "0-3", "foo[0-1023]",
        "[1000-1024]",  "4-7", "foo[1000-1024]",
        4196,
        1025,
        "foo[0-1024]",
    },
    { 0 },
};

void test_append (void)
{
    struct append_test *t = append_tests;

    while (t && t->ranksa) {
        struct rlist *rl = NULL;
        struct rlist *rl2 = NULL;
        struct hostlist *hl = NULL;
        char *s1;
        char *s2;
        char *R1 = R_create (t->ranksa, t->coresa, NULL, t->hostsa, NULL);
        char *R2 = R_create (t->ranksb, t->coresb, NULL, t->hostsb, NULL);

        if (!R1 || !R2)
            BAIL_OUT ("R_create() failed!");

        rl = rlist_from_R (R1);
        rl2 = rlist_from_R (R2);
        if (!rl || !rl2)
            BAIL_OUT ("rlist_from_R failed!");
        free (R1);
        free (R2);

        s1 = rlist_dumps (rl);
        s2 = rlist_dumps (rl2);


        ok (rlist_append (rl, rl2) == 0,
            "rlist_append: %s + %s", s1, s2);
        rlist_destroy (rl2);
        free (s1);
        free (s2);

        s1 = rlist_dumps (rl);
        diag ("result = %s", s1);
        free (s1);

        ok (rl->total == t->total_cores,
            "rlist_append: result has %d cores", rl->total);
        ok (rlist_nnodes (rl) == t->total_nodes,
            "rlist_append: result has %d nodes", rlist_nnodes (rl));

        hl = rlist_nodelist (rl);
        s1 = hostlist_encode (hl);
        is (s1, t->nodelist,
            "rlist_append: result has nodelist = %s", s1);
        free (s1);
        hostlist_destroy (hl);

        json_t *R = rlist_to_R (rl);
        R1 = json_dumps (R, JSON_COMPACT);
        diag ("%s", R1);
        json_decref (R);
        free (R1);
        rlist_destroy (rl);

        t++;
    }
}

struct append_test add_tests[] = {
    {
        "1", "0-3", "foo15",
        "0", "0-3", "foo16",
        8,
        2,
        "foo[16,15]",
    },
    {
        "0-1", "0-3", "foo[16,15]",
        "0", "0-3", "foo16",
        8,
        2,
        "foo[16,15]",
    },
    {
        "0,2-3", "0-3", "foo[0,2-3]",
        "1",     "0-3", "foo1",
        16,
        4,
        "foo[0-3]",
    },
    {
        "0", "0-3", "foo0",
        "0", "0-7", "foo0",
        8,
        1,
        "foo0",
    },
    {
        "[0-1023]", "0-3", "foo[0-1023]",
        "[1000-1024]",  "4-7", "foo[1000-1024]",
        4196,
        1025,
        "foo[0-1024]",
    },
    { 0 },
};


void test_add (void)
{
    struct append_test *t = add_tests;

    while (t && t->ranksa) {
        struct rlist *rl = NULL;
        struct rlist *rl2 = NULL;
        struct hostlist *hl = NULL;
        char *s1;
        char *s2;
        char *R1 = R_create (t->ranksa, t->coresa, NULL, t->hostsa, NULL);
        char *R2 = R_create (t->ranksb, t->coresb, NULL, t->hostsb, NULL);

        if (!R1 || !R2)
            BAIL_OUT ("R_create() failed!");

        rl = rlist_from_R (R1);
        rl2 = rlist_from_R (R2);
        if (!rl || !rl2)
            BAIL_OUT ("rlist_from_R failed!");
        free (R1);
        free (R2);

        s1 = rlist_dumps (rl);
        s2 = rlist_dumps (rl2);


        ok (rlist_add (rl, rl2) == 0,
            "rlist_add: %s + %s", s1, s2);
        rlist_destroy (rl2);
        free (s1);
        free (s2);

        s1 = rlist_dumps (rl);
        diag ("result = %s", s1);
        free (s1);

        ok (rl->total == t->total_cores,
            "rlist_add: result has %d cores", rl->total);
        ok (rlist_nnodes (rl) == t->total_nodes,
            "rlist_add: result has %d nodes", rlist_nnodes (rl));

        hl = rlist_nodelist (rl);
        s1 = hostlist_encode (hl);
        is (s1, t->nodelist,
            "rlist_add: result has nodelist = %s", s1);
        free (s1);
        hostlist_destroy (hl);

        json_t *R = rlist_to_R (rl);
        R1 = json_dumps (R, JSON_COMPACT);
        diag ("%s", R1);
        json_decref (R);
        free (R1);
        rlist_destroy (rl);

        t++;
    }
}

struct remap_test {
    const char *ranks;
    const char *cores;
    const char *gpus;
    const char *hosts;

    const char *result;
};

struct remap_test remap_tests[] = {
    {
        "1,7,9,53", "0-3", NULL, "foo[1,7,9,53]",
        "rank[0-3]/core[0-3]",
    },
    {
        "1,7,9,53", "1,5,7,9", "1,3", "foo[1,7,9,53]",
        "rank[0-3]/core[0-3],gpu[1,3]",
    },
    { 0 },
};

void test_remap ()
{
    struct remap_test *t = remap_tests;

    while (t && t->ranks) {
        char *before;
        char *after;
        struct rlist *rl;
        char *R = R_create (t->ranks, t->cores, t->gpus, t->hosts, NULL);
        if (!R)
            BAIL_OUT ("R_create failed");
        if (!(rl = rlist_from_R (R)))
            BAIL_OUT ("rlist_from_R failed");

        before = rlist_dumps (rl);
        ok (rlist_remap (rl) == 0,
                "rlist_remap (%s)", before);
        after = rlist_dumps (rl);
        is (after, t->result,
                "result = %s", after);

        free (before);
        free (after);
        rlist_destroy (rl);
        free (R);

        t++;
    }
}

struct remap_test assign_hosts_tests[] = {
    {
        "1,7,9,53", "0-3", NULL, "foo[1,7,9,53]",
        "rank[0-3]/core[0-3]",
    },
    {
        "1,7,9,53", "1,5,7,9", "1,3", "foo[1,7,9,53]",
        "rank[0-3]/core[0-3],gpu[1,3]",
    },
    { 0 },
};

void test_assign_hosts ()
{
    struct remap_test *t = assign_hosts_tests;

    while (t && t->ranks) {
        char *hosts = NULL;
        struct hostlist *hl;
        struct rlist *rl;
        char *R = R_create (t->ranks, t->cores, t->gpus, NULL, NULL);
        if (!R)
            BAIL_OUT ("R_create failed");
        if (!(rl = rlist_from_R (R)))
            BAIL_OUT ("rlist_from_R failed");

        ok (rlist_assign_hosts (rl, t->hosts) == 0,
            "rlist_assign_hosts (%s)", t->hosts);

        if (!(hl = rlist_nodelist (rl)))
            BAIL_OUT ("rlist_nodelist failed");
        if (!(hosts = hostlist_encode (hl)))
            BAIL_OUT ("hostlist_encode failed");

        is (hosts, t->hosts,
            "reassign hosts to %s worked", hosts);

        free (hosts);

        hostlist_destroy (hl);
        rlist_destroy (rl);
        free (R);

        t++;
    }

}


void test_rerank ()
{
    struct hostlist *hl = NULL;
    char *s = NULL;
    struct rlist *rl = NULL;
    flux_error_t err;
    char *R = R_create ("0-15", "0-3", NULL, "foo[0-15]", NULL);
    if (!R)
        BAIL_OUT ("R_create failed");
    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");

    ok (rlist_rerank (rl, "foo[1-15]", &err) < 0 && errno == ENOSPC,
        "rlist_rerank with too few hosts returns ENOSPC");
    is (err.text, "Number of hosts (15) is less than node count (16)",
        "rlist_rerank error message is expected");
    ok (rlist_rerank (rl, "foo[0-16]", &err) < 0 && errno == EOVERFLOW,
        "rlist_rerank with too many hosts returns EOVERFLOW");
    is (err.text, "Number of hosts (17) is greater than node count (16)",
        "rlist_rerank error message is expected");
    ok (rlist_rerank (rl, "foo[1-16]", &err) < 0 && errno == ENOENT,
        "rlist_rerank with invalid host returns ENOENT");
    is (err.text, "Host foo16 not found in resources",
        "rlist_rerank error message is expected");
    ok (rlist_rerank (rl, "foo[0-", &err) < 0 && errno == EINVAL,
        "rlist_rerank fails with invalid hostlist");
    is (err.text, "hostlist_decode: foo[0-: Invalid argument",
        "rlist_rerank error message is expected");

    if (!(hl = rlist_nodelist (rl)) || !(s = hostlist_encode (hl)))
        BAIL_OUT ("rlist_nodelist/hostlist_encode failed!");
    is (s, "foo[0-15]",
        "before: hostlist is %s", s);
    free (s);
    hostlist_destroy (hl);

    /* Swap rank 0 to rank 15 */
    ok (rlist_rerank (rl, "foo[1-15,0]", NULL) == 0,
        "rlist_rerank works");

    if (!(hl = rlist_nodelist (rl)) || !(s = hostlist_encode (hl)))
        BAIL_OUT ("rlist_nodelist/hostlist_encode failed!");
    is (s, "foo[1-15,0]",
        "after: hostlist is %s", s);
    free (s);
    hostlist_destroy (hl);

    rlist_destroy (rl);
    free (R);
}

struct op_test {
    const char *ranksa;
    const char *coresa;
    const char *gpusa;
    const char *hostsa;

    const char *ranksb;
    const char *coresb;
    const char *gpusb;
    const char *hostsb;

    const char *result;
};


struct op_test diff_tests[] = {
    {
        "0", "0-3", NULL, "foo15",
        "0", "0-3", NULL, "foo15",
        "",
    },
    {
        "0", "0-3", "0-1", "foo15",
        "0", "0-3", "0-1", "foo15",
        "",
    },
    {
        "0", "0-3", NULL, "foo15",
        "0", "0-1", NULL, "foo15",
        "rank0/core[2-3]",
    },
    {
        "0", "0-3", "0", "foo15",
        "0", "0-3", NULL, "foo15",
        "rank0/gpu0",
    },
    { 0 },
};

void test_diff ()
{
    struct op_test *t = diff_tests;

    while (t && t->ranksa) {
        struct rlist *rla = NULL;
        struct rlist *rlb = NULL;
        struct rlist *result;
        char *a;
        char *b;
        char *s;
        char *Ra = R_create (t->ranksa, t->coresa, t->gpusa, t->hostsa, NULL);
        char *Rb = R_create (t->ranksb, t->coresb, t->gpusb, t->hostsb, NULL);

        if (!Ra || !Rb)
            BAIL_OUT ("R_create() failed!");

        diag ("%s", Ra);

        rla = rlist_from_R (Ra);
        rlb = rlist_from_R (Rb);
        if (!rla || !rlb)
            BAIL_OUT ("rlist_from_R failed!");
        free (Ra);
        free (Rb);

        a = rlist_dumps (rla);
        b = rlist_dumps (rlb);

        result = rlist_diff (rla, rlb);
        if (!result)
            BAIL_OUT ("rlist_diff (%s, %s) failed", a, b);

        s = rlist_dumps (result);
        is (s, t->result,
            "rlist_diff: %s - %s = %s",
            a, b, s);

        free (a);
        free (b);
        free (s);
        rlist_destroy (result);
        rlist_destroy (rla);
        rlist_destroy (rlb);

        t++;
    }
}

struct op_test union_tests[] = {
    {
        "0", "0-3", NULL, "foo15",
        "0", "0-3", NULL, "foo15",
        "rank0/core[0-3]",
    },
    {
        "0", "0-3", "0-1", "foo15",
        "1", "0-3", "0-1", "foo16",
        "rank[0-1]/core[0-3],gpu[0-1]",
    },
    {
        "0", "0-3", NULL, "foo15",
        "0", NULL, "0", "foo15",
        "rank0/core[0-3],gpu0",
    },
    { 0 },
};


void test_union ()
{
    struct op_test *t = union_tests;

    while (t && t->ranksa) {
        struct rlist *rla = NULL;
        struct rlist *rlb = NULL;
        struct rlist *result;
        char *a;
        char *b;
        char *s;
        char *Ra = R_create (t->ranksa, t->coresa, t->gpusa, t->hostsa, NULL);
        char *Rb = R_create (t->ranksb, t->coresb, t->gpusb, t->hostsb, NULL);

        if (!Ra || !Rb)
            BAIL_OUT ("R_create() failed!");

        rla = rlist_from_R (Ra);
        rlb = rlist_from_R (Rb);
        if (!rla || !rlb)
            BAIL_OUT ("rlist_from_R failed!");
        free (Ra);
        free (Rb);

        a = rlist_dumps (rla);
        b = rlist_dumps (rlb);

        result = rlist_union (rla, rlb);
        if (!result)
            BAIL_OUT ("rlist_union (%s, %s) failed", a, b);

        s = rlist_dumps (result);
        is (s, t->result,
            "rlist_union: %s U %s = %s",
            a, b, s);

        free (a);
        free (b);
        free (s);
        rlist_destroy (result);
        rlist_destroy (rla);
        rlist_destroy (rlb);

        t++;
    }

}


struct op_test intersect_tests[] = {
    {
        "0-10", "0-3", NULL, "foo[0-10]",
        "9-15",   "1", NULL, "foo[9-15]",
        "rank[9-10]/core1",
    },
    {
        "0", "0-3", "0-1", "foo15",
        "1", "0-3", "0-1", "foo16",
        "",
    },
    { 0 },
};


void test_intersect ()
{
    struct op_test *t = intersect_tests;

    while (t && t->ranksa) {
        struct rlist *rla = NULL;
        struct rlist *rlb = NULL;
        struct rlist *result;
        char *a;
        char *b;
        char *s;
        char *Ra = R_create (t->ranksa, t->coresa, t->gpusa, t->hostsa, NULL);
        char *Rb = R_create (t->ranksb, t->coresb, t->gpusb, t->hostsb, NULL);

        if (!Ra || !Rb)
            BAIL_OUT ("R_create() failed!");

        rla = rlist_from_R (Ra);
        rlb = rlist_from_R (Rb);
        if (!rla || !rlb)
            BAIL_OUT ("rlist_from_R failed!");
        free (Ra);
        free (Rb);

        a = rlist_dumps (rla);
        b = rlist_dumps (rlb);

        result = rlist_intersect (rla, rlb);
        if (!result)
            BAIL_OUT ("rlist_intersect (%s, %s) failed", a, b);

        s = rlist_dumps (result);
        is (s, t->result,
            "rlist_intersect: %s ∩ %s = %s",
            a, b, s);

        free (a);
        free (b);
        free (s);
        rlist_destroy (result);
        rlist_destroy (rla);
        rlist_destroy (rlb);

        t++;
    }
}

void test_copy_ranks ()
{
    struct idset *ranks;
    struct rlist *rl;
    struct rlist *result;
    char *s;
    char *R = R_create ("0-5", "0-3", "0", "foo[0-5]", NULL);
    if (!R)
        BAIL_OUT ("R_create failed");
    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");
    if (!(ranks = idset_decode ("1,3,5")))
        BAIL_OUT ("idset_decode failed");

    result = rlist_copy_ranks (rl, ranks);
    if (!result)
        BAIL_OUT ("rlist_copy_ranks failed");
    ok (rlist_nnodes (result) == 3 && rlist_count (result, "core") == 12,
        "rlist_copy_ranks worked");
    s = rlist_dumps (result);
    is (s, "rank[1,3,5]/core[0-3],gpu0",
        "rlist_copy_ranks has expected result");
    free (s);
    rlist_destroy (result);
    idset_destroy (ranks);

    if (!(ranks = idset_decode ("5-9")))
        BAIL_OUT ("idset_decode failed");
    result = rlist_copy_ranks (rl, ranks);
    if (!result)
        BAIL_OUT ("rlist_copy_ranks failed");
    ok (rlist_nnodes (result) == 1 && rlist_count (result, "core") == 4,
        "rlist_copy_ranks worked");
    s = rlist_dumps (result);
    is (s, "rank5/core[0-3],gpu0",
        "rlist_copy_ranks has expected result");
    free (s);
    rlist_destroy (result);
    idset_destroy (ranks);

    if (!(ranks = idset_decode ("9,20")))
        BAIL_OUT ("idset_decode failed");
    result = rlist_copy_ranks (rl, ranks);
    if (!result)
        BAIL_OUT ("rlist_copy_ranks failed");
    ok (rlist_nnodes (result) == 0 && rlist_count (result, "core") == 0,
        "rlist_copy_ranks worked");
    s = rlist_dumps (result);
    is (s, "",
        "rlist_copy_ranks has expected result");
    free (s);
    rlist_destroy (result);
    idset_destroy (ranks);
    rlist_destroy (rl);
    free (R);
}

void test_remove_ranks ()
{
    struct idset *ranks;
    struct rlist *rl;
    char *s;
    char *R = R_create ("0-5", "0-3", "0", "foo[0-5]", NULL);
    if (!R)
        BAIL_OUT ("R_create failed");
    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");
    if (!(ranks = idset_decode ("1,3,5")))
        BAIL_OUT ("idset_decode failed");

    ok (rlist_remove_ranks (rl, ranks) == idset_count (ranks),
        "rlist_remove_ranks(1,3,5) works");

    s = rlist_dumps (rl);
    is (s, "rank[0,2,4]/core[0-3],gpu0",
        "rlist_remove_ranks: %s", s);
    free (s);
    idset_destroy (ranks);
    rlist_destroy (rl);

    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");
    if (!(ranks = idset_decode ("5-9")))
        BAIL_OUT ("idset_decode failed");
    ok (rlist_remove_ranks (rl, ranks) == 1,
        "rlist_remove_ranks (5-9)");
    s = rlist_dumps (rl);
    is (s, "rank[0-4]/core[0-3],gpu0",
        "rlist_remove_ranks: %s", s);
    free (s);
    idset_destroy (ranks);
    rlist_destroy (rl);

    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");
    if (!(ranks = idset_decode ("9,20")))
        BAIL_OUT ("idset_decode failed");
    ok (rlist_remove_ranks (rl, ranks) == 0,
        "rlist_remove_ranks (9,20) removed no ranks");
    s = rlist_dumps (rl);
    is (s, "rank[0-5]/core[0-3],gpu0",
        "rlist_remove_ranks: %s", s);
    free (s);

    idset_destroy (ranks);
    rlist_destroy (rl);
    free (R);
}


struct verify_test {
    const char *ranksa;
    const char *coresa;
    const char *gpusa;
    const char *hostsa;

    const char *ranksb;
    const char *coresb;
    const char *gpusb;
    const char *hostsb;

    int result;
    const char *errmsg;
};

struct verify_test verify_tests[] = {
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "0", "foo1",
        0,
        ""
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "",  "foo1",
        -1,
        "rank 1 (foo1) missing resources: gpu0"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "0", "foo5",
        -1,
        "rank 5 (foo5) missing resources: core[2-3]"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-3", "0", "foo7",
        -1,
        "rank 5 got hostname 'foo7', expected 'foo5'"
    },
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        1,
        "rank 0 (foo0) has extra resources: core[4-7],gpu1"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "7",   "0-3", "0", "foo7",
        -1,
        "rank 7 not found in expected ranks"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "0",   "0-3", "0",  NULL,
         0,
         "",

    },
    {
        "0-5", "0-3", "0", NULL,
        "0",   "0-3", "0", NULL,
         0,
         "",
    },
    {
        "0-5", "0-3", "0", NULL,
        "0",   "0-3", "0", "foo0",
         0,
         "",
    },
    { 0 },
};

void test_verify ()
{
    struct verify_test *t = verify_tests;

    while (t && t->ranksa) {
        int rc;
        flux_error_t error;
        struct rlist *rla = NULL;
        struct rlist *rlb = NULL;
        char *a;
        char *b;
        char *Ra = R_create (t->ranksa, t->coresa, t->gpusa, t->hostsa, NULL);
        char *Rb = R_create (t->ranksb, t->coresb, t->gpusb, t->hostsb, NULL);

        if (!Ra || !Rb)
            BAIL_OUT ("R_create() failed!");

        rla = rlist_from_R (Ra);
        rlb = rlist_from_R (Rb);
        if (!rla || !rlb)
            BAIL_OUT ("rlist_from_R failed!");
        free (Ra);
        free (Rb);

        a = rlist_dumps (rla);
        b = rlist_dumps (rlb);

        rc = rlist_verify (&error, rla, rlb);
        ok (rc == t->result,
            "rlist_verify: %s in %s = %d", b, a, rc);
        is (error.text, t->errmsg,
            "Got expected message: '%s'", error.text);

        free (a);
        free (b);
        rlist_destroy (rla);
        rlist_destroy (rlb);

        t++;
    }
}

void test_timelimits ()
{
    struct rlist *rl;
    json_t *o;
    char *R = R_create ("0-1", "0-3", NULL, "foo[0-1]", NULL);

    if (!R)
        BAIL_OUT ("R_create failed");
    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("rlist_from_R failed");

    rl->starttime = 1234.;
    rl->expiration = 2345.;

    /*  Encode to R and ensure starttime/expiration are preserved */
    if(!(o = rlist_to_R (rl)))
        BAIL_OUT ("rlist_to_R failed");

    rlist_destroy (rl);
    free (R);

    if (!(rl = rlist_from_json (o, NULL)))
        BAIL_OUT ("rlist_from_json failed");

    ok (rl->starttime == 1234. && rl->expiration == 2345.,
        "starttime and expiration preserved during encode/decode");

    json_decref (o);
    rlist_destroy (rl);
}

struct hosts_to_ranks_test {
    const char *input;
    const char *ranks;
    const char *hosts;
    const char *result;
    const char *error;
};

static struct hosts_to_ranks_test hosts_to_ranks_tests[] = {
   { "foo[0-10]",
     "0-10",
     "foo[9-11]",
     NULL,
     "invalid hosts: foo11",
    },
    { "foo[0-10]",
      "0-10",
      "foo[a-b]",
      NULL,
      "Hostlist cannot be decoded",
    },
    { "foo[0-10]",
      "0-10",
      "foo[1,7]",
      "1,7",
      NULL,
    },
    { "foo10,foo[0-4],foo11,foo[5-9]",
      "0-11",
      "foo[1,9,4]",
      "2,5,11",
      NULL,
    },
    { "foo,foo,foo,foo",
      "0-3",
      "foo",
      "0-3",
      NULL,
    },
    { 0 },
};

void test_hosts_to_ranks (void)
{
    flux_error_t err;
    struct hosts_to_ranks_test *t = hosts_to_ranks_tests;

    ok (rlist_hosts_to_ranks (NULL, NULL, &err) == NULL,
        "rlist_hosts_to_ranks returns NULL with NULL args");
    is (err.text, "An expected argument was NULL",
        "got expected error: %s", err.text);

    while (t && t->input) {
        char *R = NULL;
        struct rlist *rl = NULL;
        struct idset *ids = NULL;

        if (!(R = R_create (t->ranks, "0-1", NULL, t->input, NULL)))
            BAIL_OUT ("R_create");
        if (!(rl = rlist_from_R (R)))
            BAIL_OUT ("rlist_from_R");
        ids = rlist_hosts_to_ranks (rl, t->hosts, &err);
        if (t->result) {
            char *s = idset_encode (ids, IDSET_FLAG_RANGE);
            is (s, t->result,
                "rlist_hosts_to_ranks (rl, %s) = %s",
                t->hosts, s);
            free (s);
        }
        else {
            is (err.text, t->error,
                "to_ranks (rl, %s) got expected error: %s",
                t->hosts, err.text);
        }
        idset_destroy (ids);
        rlist_destroy (rl);
        free (R);
        t++;
    }
}

void test_issue4184 ()
{
    char *R;
    struct rlist *rl = NULL;
    struct rlist *alloc = NULL;

    if (!(R = R_create_num (4, 4))
        || !(rl = rlist_from_R (R)))
        BAIL_OUT ("issue4184: failed to create rlist");

    free (R);
    if (!(R = R_create_num (4, 4))
        || !(alloc = rlist_from_R (R)))
        BAIL_OUT ("issue4184: failed to create alloc rlist");

    ok (rlist_mark_down (rl, "all") == 0,
        "rlist_mark_down");

    ok (rl->avail == 0,
        "rlist avail = %d (expected 0)", rl->avail);

    ok (rlist_set_allocated (rl, alloc) == 0,
        "rlist_set_allocated");

    ok (rl->avail == 0,
        "rlist avail = %d (expected 0)", rl->avail);

    rlist_destroy (alloc);
    rlist_destroy (rl);
    free (R);
}

struct property_test {
    const char *desc;
    const char *ranks;
    const char *cores;
    const char *hosts;
    const char *properties;

    const char *decode_error;

    const char *constraint;

    const char *result;
};

struct property_test property_tests[] = {
    {
        "invalid properties",
        "1-10", "0-1", "foo[1-10]",
        "\"foo\"",
        "properties must be an object",
        NULL,
        NULL,
    },
    {
        "invalid properties",
        "1-10", "0-1", "foo[1-10]",
        "{ \"foo\": 1 }",
        "properties value '1' not a string",
        NULL,
        NULL,
    },
    {
        "invalid properties",
        "1-10", "0-1", "foo[1-10]",
        "{ \"foo\": \"1-30\" }",
        "ranks 11-30 not found in target resource list",
        NULL,
        NULL,
    },
    {
        "invalid properties",
        "1-10", "0-1", "foo[1-10]",
        "{ \"fo^o\": \"1-30\" }",
        "invalid character '^' in property \"fo^o\"",
        NULL,
        NULL,
    },
    {
        "invalid properties",
        "1-10", "0-1", "foo[1-10]",
        "{ \"foo\": \"x-y\" }",
        "invalid idset 'x-y' specified for property \"foo\"",
        NULL,
        NULL,
    },
    {
        "constraint: property=na",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"na\"]}",
        "",
    },
    {
        "constraint: property=foo",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"foo\"]}",
        "rank[1-3]/core[0-1]",
    },
    {
        "constraint: property=bar",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"bar\"]}",
        "rank7/core[0-1]",
    },
    {
        "constraint: property=^foo",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"^bar\"]}",
        "rank[1-6,8-10]/core[0-1]",
    },
    {
        "constraint: by hostname: foo5",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"foo5\"]}",
        "rank5/core[0-1]",
    },
    {
        "constraint: by hostname: ^foo5",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"^foo5\"]}",
        "rank[1-4,6-10]/core[0-1]",
    },
    {
        "constraint: by hostname: ^foo5",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"properties\": [\"^foo5\"]}",
        "rank[1-4,6-10]/core[0-1]",
    },
    {
        "constraint: by hostlist: foo[2,3,7]",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{\"hostlist\": [\"foo[2,3,7]\"]}",
        "rank[2-3,7]/core[0-1]",
    },
    {
        "constraint: by hostlist: not foo[2,3,7]",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{ \"not\": [{\"hostlist\": [\"foo[2,3,7]\"]}] }",
        "rank[1,4-6,8-10]/core[0-1]",
    },
    {
        "constraint: by rank: not 0-4",
        "1-10", "0-1", "foo[1-10]",
        "{\"foo\": \"1-3\", \"bar\": \"7\"}",
        NULL,
        "{ \"not\": [{\"ranks\": [\"0-4\"]}] }",
        "rank[5-10]/core[0-1]",
    },
    { 0 }
};

static void json_compare (const char *x, const char *y, const char *msg)
{
    json_t *ox = NULL;
    json_t *oy = NULL;
    json_error_t error;
    if (!(ox = json_loads (x, JSON_DECODE_ANY, &error))
        || !(oy = json_loads (y, JSON_DECODE_ANY, &error)))
        BAIL_OUT ("json_loads '%s' or '%s' failed: %s", error.text);

    ok (json_equal (ox, oy),
        "%s: %s", msg, x);

    json_decref (ox);
    json_decref (oy);
}

/*  Note: this test only does some simple sanity checks.
 *   More extensive testing will be contained in flux-R driven tests.
 */
void test_properties (void)
{
    struct rlist *rl;
    struct rlist *cpy;
    struct rlist *rlc;
    char *R;
    char *s;
    json_t *Rj;
    flux_error_t error;
    json_error_t jerr;

    ok (rlist_assign_properties (NULL, NULL, &error) < 0 && errno == EINVAL,
        "rlist_assign_properties (NULL, NULL) fails");
    is (error.text, "Invalid argument",
        "fails with \"Invalid argument\"");

    ok (rlist_properties_encode (NULL) == NULL && errno == EINVAL,
        "rlist_properties_encode (NULL) returns EINVAL");

    rl = rlist_create ();
    s = rlist_properties_encode (rl);
    is (s, "{}",
        "rlist_properties_encode on empty rlist returns empty object");
    free (s);

    if (rlist_append_rank_cores (rl, "foo0", 0, "0-3") < 0)
        BAIL_OUT ("rlist_append_rank_cores failed: %s", strerror (errno));

    s = rlist_properties_encode (rl);
    is (s, "{}",
        "rlist_properties_encode with no properties returns empty object");
    free (s);
    rlist_destroy (rl);

    struct property_test *t = property_tests;
    while (t->desc) {
        if (!(R = R_create (t->ranks,
                            t->cores,
                            NULL,
                            t->hosts,
                            t->properties)))
            BAIL_OUT ("%s: R_create failed!", t->desc);

        if (!(Rj = json_loads (R, 0, NULL)))
            BAIL_OUT ("%s: json_loads (R) failed", t->desc);

        if (!(rl  = rlist_from_json (Rj, &jerr))) {
            if (t->decode_error) {
                is (jerr.text, t->decode_error,
                    "%s: %s",
                    t->desc,
                    jerr.text);
                free (R);
                json_decref (Rj);
                ++t;
                continue;
            }
            BAIL_OUT ("%s: rlist_from_R() failed!", t->desc);
        }

        /*  Return R from rl and ensure it can be decoded again.
         */
        free (R);
        if (!(R = rlist_encode (rl)))
            BAIL_OUT ("%s: rlist_encode() failed!", t->desc);
        if (!(cpy = rlist_from_R (R)))
            BAIL_OUT ("%s: rlist_from_R() after rlist_encode() failed!",
                      t->desc);

        /*  Use cpy in place of original rlist to ensure that encode/decode
         *   preserves expected properties.
         */
        rlist_destroy (rl);
        rl = cpy;

        /*  Check that rlist_properties_encode() works
         */
        char *p = rlist_properties_encode (rl);
        json_compare (p, t->properties,
                      "rlist_properties_encode");
        free (p);

        rlc = rlist_copy_constraint_string (rl, t->constraint, &error);
        ok (rlc != NULL,
            "rlist_copy_constraint works: %s",
            rlc ? "ok" : error.text);
        s = rlist_dumps (rlc);
        is (s, t->result, "%s: %s", t->desc, s);

        free (R);
        free (s);
        json_decref (Rj);
        rlist_destroy (rl);
        rlist_destroy (rlc);
        t++;
    }

}

static void test_issue4290 (void)
{
    char *R;
    char *s;
    struct rlist *result;
    struct rlist *rl;
    flux_error_t error;
    struct rlist_alloc_info ai = {
        .nnodes = 4,
        .slot_size = 1,
        .nslots = 4,
        .exclusive = true,
    };

    if (!(R = R_create ("0-3",
                        "0-3",
                        NULL,
                        "foo[0-3]",
                        NULL)))
        BAIL_OUT ("issue4290: R_create");

    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("issue4290: rlist_from_R() failed");
    if (rlist_mark_down (rl, "2") < 0)
        BAIL_OUT ("issue4290: error marking rank 2 down");
    result = rlist_alloc (rl, &ai, &error);
    ok (!result && errno == ENOSPC,
        "issue4290: alloc 4/4 nodes with node down fails with ENOSPC");
    ok (rlist_mark_up (rl, "2") == 0,
        "issue4290: marking rank 2 up");
    ok ((result = rlist_alloc (rl, &ai, &error)) != NULL,
        "issue4290: now allocation succeeds");
    s = rlist_dumps (result);
    diag ("%s", s);

    free (s);
    rlist_destroy (result);
    rlist_destroy (rl);
    free (R);
}

static void test_rlist_config_inval (void)
{
    flux_error_t error;
    json_t *o;

    ok (rlist_from_config (NULL, &error) == NULL,
        "rlist_from_config (NULL) fails");
    is (error.text, "resource config must be an array",
        "error.text is expected: %s",
        error.text);

    if (!(o = json_object()))
        BAIL_OUT ("test_rlist_config_inval: json_object: %s", strerror (errno));
    ok (rlist_from_config (o, &error) == NULL,
        "rlist_from_config() with empty object fails");
    is (error.text, "resource config must be an array",
        "error.text is expected: %s",
        error.text);
    json_decref (o);

    if (!(o = json_array ()))
        BAIL_OUT ("test_rlist_config_inval: json_array: %s", strerror (errno));
    ok (rlist_from_config (o, &error) == NULL,
        "rlist_from_config() with empty array fails");
    is (error.text, "no hosts configured",
        "error.text is expected: %s",
        error.text);
    json_decref (o);
}

static void test_issue_5868 (void)
{
    char *R;
    char *s;
    struct rlist *rl;
    struct idset *ranks;

    if (!(R = R_create ("0-3",
                        "0-3",
                        NULL,
                        "foo[0-3]",
                        NULL)))
        BAIL_OUT ("issue5868: R_create");

    if (!(rl = rlist_from_R (R)))
        BAIL_OUT ("issue5868: rlist_from_R() failed");
    /*  Remove ranks 0-1
     */
    if (!(ranks = idset_decode ("0-1")))
        BAIL_OUT ("issue5868: idset_create failed");
    if (rlist_remove_ranks (rl, ranks) < 0)
        BAIL_OUT ("issue5868: rlist_remove_ranks failed");
    idset_destroy (ranks);

    ok (rlist_mark_down (rl, "0-2") == 0,
        "issue5868: rlist_mark_down (0-2) ignores missing ranks");

    s = rlist_dumps (rl);
    diag ("%s", s);
    is (s, "rank3/core[0-3]",
        "issue5868: expected resources remain up");

    free (s);
    rlist_destroy (rl);
    free (R);
}

struct core_spec_test {
    const char *ranks;
    const char *cores;
    const char *hosts;

    const char *spec;
    const char *result;
    const char *error;
};

struct core_spec_test core_spec_tests[] = {
    { "0-3", "0-3", "foo[0-3]", "0",     "rank[0-3]/core0",     NULL },
    { "0-3", "0-3", "foo[0-3]", "0-1",   "rank[0-3]/core[0-1]", NULL },
    { "0-3", "0-3", "foo[0-3]", "0@0",   "rank0/core0",         NULL },
    { "0-3", "0-3", "foo[0-3]", "0,2@0", "rank0/core[0,2]",     NULL },
    { "0-3", "0-3", "foo[0-3]", "0@0-1", "rank[0-1]/core0",     NULL },
    { "0-3", "0-3", "foo[0-3]", "0-7@0", "rank0/core[0-3]",     NULL },
    { "0-3",
      "0-3",
      "foo[0-3]",
      "0-3@0 0@1-3",
      "rank0/core[0-3] rank[1-3]/core0",
      NULL },
    { "0-3", "0-3", "foo[0-3]", "foo",   NULL, "error parsing range 'foo'"},
    { "0-3", "0-3", "foo[0-3]", "0@",    NULL, "ranks/cores cannot be empty"},
    { "0-3", "0-3", "foo[0-3]", "@0",    NULL, "ranks/cores cannot be empty"},
    { "0-3", "0-3", "foo[0-3]", "0 0@",  NULL, "ranks/cores cannot be empty"},
    { NULL, NULL, NULL, NULL, NULL, NULL },
};

static void test_core_spec (void)
{
    struct core_spec_test *te = &core_spec_tests[0];
    while (te && te->ranks) {
        char *R;
        struct rlist *rl;
        struct rlist *result;
        flux_error_t error;

        if (!(R = R_create (te->ranks, te->cores, NULL, te->hosts, NULL)))
            BAIL_OUT ("test_core_spec: R_create");

        if (!(rl = rlist_from_R (R)))
            BAIL_OUT ("test_core_spec: rlist_from_R() failed");

        result = rlist_copy_core_spec (rl, te->spec, &error);
        if (result) {
            char *s = rlist_dumps (result);
            pass ("rlist_copy_core_spec (%s) returned %s", te->spec, s);
            if (te->result)
                is (s, te->result, "got expected result");
            else
                fail ("got %s but expected failure", s);
            free (s);
        }
        else if (te->error) {
            pass ("rlist_copy_core_spec (%s) failed as expected", te->spec);
            is (error.text, te->error, "got expected error: %s", error.text);
        }
        else
            diag ("rlist_copy_core_spec (%s): %s", te->spec, error.text);
        free (R);
        rlist_destroy (rl);
        rlist_destroy (result);
        te++;
    }
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_simple ();
    test_dumps ();
    run_test_entries (test_2n_4c,       2, 4);
    run_test_entries (test_6n_4c,       6, 4);
    run_test_entries (test_1024n_4c, 1024, 4);
    run_test_entries (test_exclusive,   4, 4);
    test_issue2202 ();
    test_issue2473 ();
    test_updown ();
    test_append ();
    test_add ();
    test_diff ();
    test_union ();
    test_intersect ();
    test_copy_ranks ();
    test_remove_ranks ();
    test_verify ();
    test_timelimits ();
    test_remap ();
    test_assign_hosts ();
    test_rerank ();
    test_hosts_to_ranks ();
    test_issue4184 ();
    test_properties ();
    test_issue4290 ();
    test_rlist_config_inval ();
    test_issue_5868 ();
    test_core_spec ();
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
