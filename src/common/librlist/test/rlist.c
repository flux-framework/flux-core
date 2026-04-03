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

    const char *config;

    int result;
    const char *errmsg;
};

struct verify_test verify_tests[] = {
    // Existing tests with NULL config (backward compatibility)
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "0", "foo1",
        NULL,
        0,
        ""
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "",  "foo1",
        NULL,
        -1,
        "rank 1 (foo1) missing resources: gpu0"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "0", "foo5",
        NULL,
        -1,
        "rank 5 (foo5) missing resources: core[2-3]"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-3", "0", "foo7",
        NULL,
        -1,
        "rank 5 got hostname 'foo7', expected 'foo5'"
    },
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        NULL,
        1,
        "rank 0 (foo0) has extra resources: core[4-7],gpu1"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "7",   "0-3", "0", "foo7",
        NULL,
        -1,
        "rank 7 not found in expected ranks"
    },
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "0",   "0-3", "0",  NULL,
        NULL,
         0,
         "",
    },
    {
        "0-5", "0-3", "0", NULL,
        "0",   "0-3", "0", NULL,
        NULL,
         0,
         "",
    },
    {
        "0-5", "0-3", "0", NULL,
        "0",   "0-3", "0", "foo0",
        NULL,
         0,
         "",
    },
    // New tests: missing GPUs with ignore
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "",  "foo1",
        "{\"gpu\":\"ignore\"}",
        0,
        ""
    },
    // New tests: missing GPUs with allow-missing (still fails)
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "1",   "0-3", "",  "foo1",
        "{\"gpu\":\"allow-missing\"}",
        0,
        ""
    },
    // New tests: missing cores with ignore
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "0", "foo5",
        "{\"core\":\"ignore\"}",
        0,
        ""
    },
    // New tests: missing cores with allow-missing
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "0", "foo5",
        "{\"core\":\"allow-missing\"}",
        0,
        ""
    },
    // New tests: missing cores still fails with allow-extra
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "0", "foo5",
        "{\"core\":\"allow-extra\"}",
        -1,
        "rank 5 (foo5) missing resources: core[2-3]"
    },
    // New tests: hostname mismatch with ignore
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-3", "0", "foo7",
        "{\"hostname\":\"ignore\"}",
        0,
        ""
    },
    // New tests: extra resources with allow-extra
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"default\":\"allow-extra\"}",
        0,
        ""
    },
    // New tests: extra cores allowed, extra GPUs fail
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"core\":\"allow-extra\"}",
        1,
        "rank 0 (foo0) has extra resources: gpu1"
    },
    // New tests: extra GPUs allowed, extra cores fail
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"gpu\":\"allow-extra\"}",
        1,
        "rank 0 (foo0) has extra resources: core[4-7]"
    },
    // New tests: both extra cores and GPUs allowed
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"core\":\"allow-extra\",\"gpu\":\"allow-extra\"}",
        0,
        ""
    },
    // New tests: extra resources with allow-missing fails
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"default\":\"allow-missing\"}",
        1,
        "rank 0 (foo0) has extra resources: core[4-7],gpu1"
    },
    // New tests: extra cores with allow-missing fails
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0", "foo0",
        "{\"core\":\"allow-missing\"}",
        1,
        "rank 0 (foo0) has extra resources: core[4-7]"
    },
    // New tests: extra GPUs allowed, missing cores allowed
    {
        "0-5", "0-7", "0",   "foo[0-5]",
        "0",   "0-3", "0-1", "foo0",
        "{\"core\":\"allow-missing\",\"gpu\":\"allow-extra\"}",
        0,
        ""
    },
    // New tests: ignore all verification
    {
        "0-5", "0-3", "0", "foo[0-5]",
        "5",   "0-1", "",  "foo7",
        "{\"default\":\"ignore\"}",
        0,
        ""
    },
    // New tests: strict (default) with explicit config
    {
        "0-5", "0-3", "0",   "foo[0-5]",
        "0",   "0-7", "0-1", "foo0",
        "{\"default\":\"strict\"}",
        1,
        "rank 0 (foo0) has extra resources: core[4-7],gpu1"
    },
    // New tests: allow extra cores but still drain on missing
    {
        "0-5", "0-7", "0", "foo[0-5]",
        "0",   "0-3", "0", "foo0",
        "{\"core\":\"allow-extra\"}",
        -1,
        "rank 0 (foo0) missing resources: core[4-7]"
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
        struct rlist_verify_config *config = NULL;
        json_t *verify_obj = NULL;
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

        // Parse verify config if provided
        if (t->config) {
            json_error_t json_error;
            if (!(verify_obj = json_loads (t->config,
                                           JSON_DECODE_ANY,
                                           &json_error)))
                BAIL_OUT ("invalid verify config in test: %s",
                         json_error.text);
            if (!(config = rlist_verify_config_create (verify_obj,
                                                       &error))) {
                json_decref (verify_obj);
                BAIL_OUT ("rlist_verify_config_create failed: %s",
                         error.text);
            }
        }

        a = rlist_dumps (rla);
        b = rlist_dumps (rlb);

        rc = rlist_verify_ex (&error, rla, rlb, config);
        ok (rc == t->result,
            "rlist_verify_ex: %s in %s (config=%s) = %d",
            b,
            a,
            t->config ? t->config : "NULL",
            rc);
        is (error.text, t->errmsg,
            "Got expected message: '%s'", error.text);

        free (a);
        free (b);
        rlist_verify_config_destroy (config);
        json_decref (verify_obj);
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

        json_t *co = json_loads (t->constraint, 0, NULL);
        if (!co)
            BAIL_OUT ("%s: json_loads (constraint) failed", t->desc);
        rlc = rlist_copy_constraint (rl, co, &error);
        json_decref (co);
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

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_dumps ();
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
    test_properties ();
    test_rlist_config_inval ();
    test_issue_5868 ();
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
