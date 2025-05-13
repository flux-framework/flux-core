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
#include <errno.h>

#include <flux/taskmap.h>
#include "taskmap_private.h"

#include "src/common/libtap/tap.h"

struct test_args {
    const char *input;
    int nnodes;
    int total_nnodes;
    int total_ntasks;
    char *idsets[24];
};

struct test_vector {
    const char *taskmap;
    const char *expected;
};

struct test_vector rfc34_test_vectors[] = {
    { "[]", "" },
    { "[[0,1,1,1]]",
      "0" },
    { "[[0,2,1,1]]",
      "0;1" },
    { "[[0,1,2,1]]",
      "0-1" },
    { "[[0,2,2,1]]",
      "0-1;2-3" },
    { "[[0,2,1,2]]",
      "0,2;1,3" },
    { "[[1,1,1,1],[0,1,1,1]]",
      "1;0" },
    { "[[0,4,4,1]]",
      "0-3;4-7;8-11;12-15" },
    { "[[0,4,1,4]]",
      "0,4,8,12;1,5,9,13;2,6,10,14;3,7,11,15" },
    { "[[0,4,2,2]]",
      "0-1,8-9;2-3,10-11;4-5,12-13;6-7,14-15" },
    { "[[0,4,2,1],[4,2,4,1]]",
      "0-1;2-3;4-5;6-7;8-11;12-15" },
    { "[[0,6,1,2],[4,2,1,2]]",
      "0,6;1,7;2,8;3,9;4,10,12,14;5,11,13,15" },
    { "[[5,1,4,1],[4,1,4,1],[3,1,2,1],[2,1,2,1],[1,1,2,1],[0,1,2,1]]",
      "14-15;12-13;10-11;8-9;4-7;0-3" },
    { "[[0,5,2,1],[6,1,2,1],[5,1,2,1],[7,1,2,1]]",
      "0-1;2-3;4-5;6-7;8-9;12-13;10-11;14-15" },
    { "[[3,1,4,1],[2,1,4,1],[1,1,4,1],[0,1,4,1]]",
      "12-15;8-11;4-7;0-3" },
    { NULL, NULL },
};

static void rfc34_tests ()
{
    struct test_vector *t;
    for (t = &rfc34_test_vectors[0]; t->taskmap != NULL; t++) {
        char *s;
        struct taskmap *map = taskmap_decode (t->taskmap, NULL);
        if (!map)
            BAIL_OUT("taskmap_decode failed!");
        ok (map != NULL,
            "taskmap_decode (%s)",
            t->taskmap);
        ok ((s = taskmap_encode (map, TASKMAP_ENCODE_RAW)) != NULL,
            "taskmap_encode_raw works");
        is (s, t->expected,
            "taskmap raw=%s",
            s);
        if (strlen (s))
            ok (taskmap_unknown (map) == false,
                "taskmap is known");
        taskmap_destroy (map);
        free (s);

        /*  Try raw back to taskmap:
         */
        map = taskmap_decode (t->expected, NULL);
        ok (map != NULL,
            "taskmap_decode (%s)",
            t->expected);
        if (map) {
            ok ((s = taskmap_encode (map, 0)) != NULL,
            "taskmap_encode works");
            is (s, t->taskmap,
                "taskmap=%s",
                s);
            taskmap_destroy (map);
            free (s);
        }
    }
}

struct test_vector pmi_tests[] = {
    { "[]", "" },
    { "[[0,4,4,1]]", "(vector,(0,4,4))" },
    { "[[0,4,2,1],[4,2,4,1]]", "(vector,(0,4,2),(4,2,4))" },
    { "[[0,4,1,4]]", "(vector,(0,4,1),(0,4,1),(0,4,1),(0,4,1))" },
    { "[[0,4096,256,1]]", "(vector,(0,4096,256))" },
    { NULL, NULL },
};

struct test_vector pmi_decode_tests[] = {
    { "", "[]" },
    { "(vector,(0,1,4))", "[[0,1,4,1]]" },
    { "(vector,(0,2,2))", "[[0,2,2,1]]" },
    { "(vector,(0,16,16))", "[[0,16,16,1]]" },
    { "(vector,(0,8,16),(0,4,32))", "[[0,8,16,1],[0,4,32,1]]" },
    { "(vector,(0,4,2),(1,3,1))", "[[0,4,2,1],[1,3,1,1]]" },
    { "(vector,(0,4,1),(0,4,1),(0,4,1),(0,4,1))", "[[0,4,1,4]]" },
    { "(vector,(0,4,4),(0,4,1))", "[[0,4,4,1],[0,4,1,1]]" },
    { "    (vector, (0,4,4), (0,4,1), )", "[[0,4,4,1],[0,4,1,1]]" },
    { "(vector, (1,1,1), (0,2,2))", "[[1,1,1,1],[0,2,2,1]]" },
    { "(vector, (1,1,1), (0,2,2),)", "[[1,1,1,1],[0,2,2,1]]" },
    { "(vector, (0,1,1), (1,5,3), (6,2, 5))",
      "[[0,1,1,1],[1,5,3,1],[6,2,5,1]]" },
    { NULL, NULL },
};

struct test_vector pmi_invalid[] = {
    { "vector, (1,1))", "unable to parse block: (1,1))" },
    { "(vector, (1.11, 2.2))", "unable to parse block: (1.11, 2.2))" },
    { "(vector, (1,1,0))", "invalid number in block: (1,1,0))" },
    { "((1,1,1))", "invalid token near '('" },
    { "((1,1,1), vector,)", "vector prefix must precede blocklist" },
    { NULL, NULL },
};

static void pmi_mapping_tests ()
{
    struct test_vector *t;
    for (t = &pmi_tests[0]; t->taskmap != NULL; t++) {
        char *s;
        struct taskmap *map2;
        struct taskmap *map = taskmap_decode (t->taskmap, NULL);
        if (!map)
            BAIL_OUT("taskmap_decode failed!");
        ok (map != NULL,
            "taskmap_decode (%s)",
            t->taskmap);
        ok ((s = taskmap_encode (map, TASKMAP_ENCODE_PMI)) != NULL,
            "taskmap_encode_pmi works");
        is (s, t->expected,
            "taskmap pmi=%s",
            s);
        ok ((map2 = taskmap_decode (s, NULL)) != NULL,
            "taskmap_decode (%s)",
            s);
        free (s);
        ok ((s = taskmap_encode (map, 0)) != NULL,
            "taskmap_encode works");
        is (s, t->taskmap,
            "taskmap=%s",
            s);
        taskmap_destroy (map);
        taskmap_destroy (map2);
        free (s);
    }

    for (t = &pmi_decode_tests[0]; t->taskmap != NULL; t++) {
        char *s;
        struct taskmap *map = taskmap_decode (t->taskmap, NULL);
        if (!map)
            BAIL_OUT ("taskmap_decode failed!");
        ok (map != NULL,
            "taskmap_decode (%s)",
            t->taskmap);
        ok ((s = taskmap_encode (map, 0)) != NULL,
            "taskmap_encode works");
        is (s, t->expected,
            "taskmap map=%s",
            s);
        taskmap_destroy (map);
        free (s);
    }

    for (t = &pmi_invalid[0]; t->taskmap != NULL; t++) {
        flux_error_t error;
        ok (taskmap_decode (t->taskmap, &error) == NULL,
            "taskmap_decode (%s) fails",
            t->taskmap);
        is (error.text, t->expected,
            "got error %s",
            error.text);
    }
}

struct test_args tests[] = {
    { "[[0,2,2,1]]", 2, 2, 4, { "0-1", "2-3" } },
    { "[[0,2,1,2]]", 2, 2, 4, { "0,2", "1,3" } },
    { "[[0,16,16,1]]", 16, 16, 256,
      { "0-15", "16-31", "32-47", "48-63", "64-79", "80-95",
        "96-111", "112-127", "128-143", "144-159", "160-175",
        "176-191", "192-207", "208-223", "224-239", "240-255",
        NULL,
      },
    },
    { "[[0,8,16,1],[8,4,32,1]]", 12, 12, 256,
      { "0-15", "16-31", "32-47", "48-63", "64-79", "80-95",
        "96-111", "112-127", "128-159", "160-191", "192-223", "224-255",
        NULL
      },
    },
    { "[[0,4096,1,256]]", 2, 4096, 1048576,
      { "0,4096,8192,12288,16384,20480,24576,28672,32768,36864,40960,45056,49152,53248,57344,61440,65536,69632,73728,77824,81920,86016,90112,94208,98304,102400,106496,110592,114688,118784,122880,126976,131072,135168,139264,143360,147456,151552,155648,159744,163840,167936,172032,176128,180224,184320,188416,192512,196608,200704,204800,208896,212992,217088,221184,225280,229376,233472,237568,241664,245760,249856,253952,258048,262144,266240,270336,274432,278528,282624,286720,290816,294912,299008,303104,307200,311296,315392,319488,323584,327680,331776,335872,339968,344064,348160,352256,356352,360448,364544,368640,372736,376832,380928,385024,389120,393216,397312,401408,405504,409600,413696,417792,421888,425984,430080,434176,438272,442368,446464,450560,454656,458752,462848,466944,471040,475136,479232,483328,487424,491520,495616,499712,503808,507904,512000,516096,520192,524288,528384,532480,536576,540672,544768,548864,552960,557056,561152,565248,569344,573440,577536,581632,585728,589824,593920,598016,602112,606208,610304,614400,618496,622592,626688,630784,634880,638976,643072,647168,651264,655360,659456,663552,667648,671744,675840,679936,684032,688128,692224,696320,700416,704512,708608,712704,716800,720896,724992,729088,733184,737280,741376,745472,749568,753664,757760,761856,765952,770048,774144,778240,782336,786432,790528,794624,798720,802816,806912,811008,815104,819200,823296,827392,831488,835584,839680,843776,847872,851968,856064,860160,864256,868352,872448,876544,880640,884736,888832,892928,897024,901120,905216,909312,913408,917504,921600,925696,929792,933888,937984,942080,946176,950272,954368,958464,962560,966656,970752,974848,978944,983040,987136,991232,995328,999424,1003520,1007616,1011712,1015808,1019904,1024000,1028096,1032192,1036288,1040384,1044480",
       "1,4097,8193,12289,16385,20481,24577,28673,32769,36865,40961,45057,49153,53249,57345,61441,65537,69633,73729,77825,81921,86017,90113,94209,98305,102401,106497,110593,114689,118785,122881,126977,131073,135169,139265,143361,147457,151553,155649,159745,163841,167937,172033,176129,180225,184321,188417,192513,196609,200705,204801,208897,212993,217089,221185,225281,229377,233473,237569,241665,245761,249857,253953,258049,262145,266241,270337,274433,278529,282625,286721,290817,294913,299009,303105,307201,311297,315393,319489,323585,327681,331777,335873,339969,344065,348161,352257,356353,360449,364545,368641,372737,376833,380929,385025,389121,393217,397313,401409,405505,409601,413697,417793,421889,425985,430081,434177,438273,442369,446465,450561,454657,458753,462849,466945,471041,475137,479233,483329,487425,491521,495617,499713,503809,507905,512001,516097,520193,524289,528385,532481,536577,540673,544769,548865,552961,557057,561153,565249,569345,573441,577537,581633,585729,589825,593921,598017,602113,606209,610305,614401,618497,622593,626689,630785,634881,638977,643073,647169,651265,655361,659457,663553,667649,671745,675841,679937,684033,688129,692225,696321,700417,704513,708609,712705,716801,720897,724993,729089,733185,737281,741377,745473,749569,753665,757761,761857,765953,770049,774145,778241,782337,786433,790529,794625,798721,802817,806913,811009,815105,819201,823297,827393,831489,835585,839681,843777,847873,851969,856065,860161,864257,868353,872449,876545,880641,884737,888833,892929,897025,901121,905217,909313,913409,917505,921601,925697,929793,933889,937985,942081,946177,950273,954369,958465,962561,966657,970753,974849,978945,983041,987137,991233,995329,999425,1003521,1007617,1011713,1015809,1019905,1024001,1028097,1032193,1036289,1040385,1044481",
       NULL },
    },
    { NULL, 0, 0, 0, {0} },
};

static bool check_all_tasks (struct taskmap *map,
                             const struct idset *taskids,
                             int nodeid)
{
    unsigned int i = idset_first (taskids);

    while (i != IDSET_INVALID_ID) {
        int n = taskmap_nodeid (map, i);
        if (n != nodeid) {
            fail ("task %u is on node %d (expected %d)",
                  i,
                  n,
                  nodeid);
            return false;
        }
        i = idset_next (taskids, i);
    }
    return true;
}

static void main_tests ()
{
    struct test_args *t;

    for (t = &tests[0]; t->input != NULL; t++) {
        flux_error_t error;
        char *s;
        struct taskmap *map = taskmap_decode (t->input, &error);
        if (!map)
            BAIL_OUT ("taskmap_decode(%s): %s", t->input, error.text);
        ok (map != NULL,
            "taskmap_decode (%s)",
            t->input);
        ok (taskmap_nnodes (map) == t->total_nnodes,
            "taskmap_nnodes returned %d (expected %d)",
            taskmap_nnodes (map),
            t->total_nnodes);
        ok (taskmap_total_ntasks (map) == t->total_ntasks,
            "taskmap_total_ntasks returned %d (expected %d)",
            taskmap_total_ntasks (map),
            t->total_ntasks);
        ok ((s = taskmap_encode (map, 0)) != NULL,
            "taskmap_encode works");
        is (s, t->input,
            "taskmap_encode returns expected string: %s", s);
        free (s);
        for (int i = 0; i < t->nnodes; i++) {
            const struct idset *taskids = taskmap_taskids (map, i);
            if (!taskids)
                BAIL_OUT ("taskmap_taskids (%s, %d) failed", t->input, i);
            char *s = idset_encode (taskids, IDSET_FLAG_RANGE);
            is (s, t->idsets[i],
                "node %d idset is %s", i, s);

            ok (check_all_tasks (map, taskids, i),
                "%d tasksids on nodeid %d",
                idset_count (taskids), i);

            free (s);
        }
        taskmap_destroy (map);
    }
}

static const char *invalid[] = {
    "}",
    "{}",
    "{\"version\":1}",
    "{\"version\":1,\"map\":{}}",
    "{\"version\":2,\"map\":[[1,1,1,1]]}",
    "{\"version\":1,\"map\":[[]]}",
    "{\"version\":1,\"map\":[[\"1\",\"1\",\"1\"]]}",
    "[[-1,1,1,1]]",
    "[[0,1,1,1],[-1,1,1,1]]",
    "[[0,1,1,1],1]",
    NULL
};

static void error_tests ()
{
    struct taskmap *map;
    flux_error_t error;

    if (!(map = taskmap_create ()))
        BAIL_OUT ("taskmap_create");

    /* Test "unknown" task map errors */
    ok (taskmap_unknown (map),
        "taskmap_unknown returns true for empty task map");
    ok (taskmap_nnodes (map) < 0 && errno == EINVAL,
        "taskmap_nnodes on unknown taskmap returns EINVAL");
    ok (taskmap_total_ntasks (map) < 0 && errno == EINVAL,
        "taskmap_nnodes on unknown taskmap returns EINVAL");
    ok (taskmap_nodeid (map, 0) < 0 && errno == EINVAL,
        "taskmap_nodeid on unknown taskmap returns EINVAL");
    ok (taskmap_taskids (map, 0) == NULL && errno == EINVAL,
        "taskmap_taskids on unknown taskmap returns EINVAL");

    /* Add one task to taskmap so it is no longer unknown */
    ok (taskmap_append (map, 0, 1, 1) == 0,
        "add one task to taskmap so it is no longer unknown");

    ok (taskmap_encode (NULL, 0) == NULL && errno == EINVAL,
        "taskmap_encode (NULL) returns EINVAL");
    ok (taskmap_encode (map, 0xff) == NULL && errno == EINVAL,
        "taskmap_encode (map, 0xff) returns EINVAL");
    ok (taskmap_encode (map, TASKMAP_ENCODE_RAW | TASKMAP_ENCODE_PMI) == NULL
        && errno == EINVAL,
        "taskmap_encode (map, MULTIPLE_ENCODINGS) returns EINVAL");

    ok (taskmap_taskids (map, -1) == NULL && errno == EINVAL,
        "taskmap_taskids (map, -1) returns EINVAL");
    ok (taskmap_taskids (map, 1) == NULL && errno == ENOENT,
        "taskmap_taskids (map, 1) returns ENOENT");

    ok (taskmap_nodeid (NULL, 0) < 0 && errno == EINVAL,
        "Ttaskmap_nodeid (NULL, 0) returns EINVAL");
    ok (taskmap_nodeid (map, -1) < 0 && errno == EINVAL,
        "Ttaskmap_nodeid (map, -1) returns EINVAL");

    ok (taskmap_ntasks (NULL, 0) < 0 && errno == EINVAL,
        "taskmap_ntasks (NULL) returns EINVAL");
    ok (taskmap_ntasks (map, -1) < 0 && errno == EINVAL,
        "taskmap_ntasks (map, -1) returns EINVAL");
    ok (taskmap_ntasks (map, 1) < 0 && errno == ENOENT,
        "taskmap_ntasks (map, 1) returns ENOENT");

    ok (taskmap_nnodes (NULL) < 0 && errno == EINVAL,
        "taskmap_nnodes (NULL) returns EINVAL");
    ok (taskmap_total_ntasks (NULL) < 0 && errno == EINVAL,
        "taskmap_total_ntasks (NULL) returns EINVAL");

    ok (taskmap_decode (NULL, &error) == NULL,
        "taskmap_decode (NULL) fails");
    is (error.text, "Invalid argument",
        "taskmap_decode (NULL) sets error.text=%s",
        error.text);

    ok (taskmap_decode_json (NULL, &error) == NULL,
        "taskmap_decode_json (NULL) fails");
    is (error.text, "Invalid argument",
        "taskmape_decode_json (NULL) sets error.text=%s",
        error.text);

    /* Do not try to match jansson errors exactly */
    for (const char **input = &invalid[0]; *input != NULL; input++) {
        ok (taskmap_decode (*input, &error) == NULL,
            "taskmap_decode (%s) fails with %s",
            *input,
            error.text);
    }

    ok (taskmap_append (NULL, 0, 0, 0) < 0 && errno == EINVAL,
        "taskmap_append (NULL) returns EINVAL");
    ok (taskmap_append (map, 0, 0, 0) < 0 && errno == EINVAL,
        "taskmap_append (NULL, 0, 0, 0) returns EINVAL");

    taskmap_destroy (map);
}

void append_tests ()
{
    int n;
    char *s;
    struct taskmap *map = taskmap_create ();
    if (!map)
        BAIL_OUT ("taskmap_create");

    for (int i = 0; i < 4; i++) {
        ok (taskmap_append (map, i, 1, 4) == 0,
            "taskmap_append (%d, 1, 4)",
            i);
    }
    n = taskmap_nnodes (map);
    ok (n == 4,
        "taskmap_nnodes() == 4 (got %d)",
        n);
    n = taskmap_total_ntasks (map);
    ok (n == 16,
        "taskmap_nnodes() == 16 (got %d)",
        n);
    s = taskmap_encode (map, 0);
    ok (s != NULL,
        "taskmap_encode works");
    is (s, "[[0,4,4,1]]",
        "map = %s",
        s);
    free (s);

    /* Add another couple nodes with higher tasks-per-node count */
    for (int i = 4; i < 6; i++) {
        ok (taskmap_append (map, i, 1, 8) == 0,
            "taskmap_append (%d, 1, 8)",
            i);
    }

    n = taskmap_nnodes (map);
    ok (n == 6,
        "taskmap_nnodes() == 6 (got %d)",
        n);
    n = taskmap_total_ntasks (map);
    ok (n == 32,
        "taskmap_nnodes() == 32 (got %d)",
        n);
    s = taskmap_encode (map, 0);
    ok (s != NULL,
        "taskmap_encode works");
    is (s, "[[0,4,4,1],[4,2,8,1]]",
        "map = %s",
        s);
    free (s);

    /* Add one more block of nodes that matches previous block */
    ok (taskmap_append (map, 4, 2, 8) == 0,
        "taskmap_append (4, 2, 8)");
    s = taskmap_encode (map, 0);
    ok (s != NULL,
        "taskmap_encode works");
    is (s, "[[0,4,4,1],[4,2,8,2]]",
        "map = %s",
        s);

    free (s);
    taskmap_destroy (map);
}

void append_cyclic_test ()
{
    int n;
    char *s;
    struct taskmap *map = taskmap_create ();
    if (!map)
        BAIL_OUT ("taskmap_create");

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ok (taskmap_append (map, j, 1, 1) == 0,
                "taskmap_append (%d, 1, 1)", j);
        }
    }
    n = taskmap_nnodes (map);
    ok (n == 4,
        "taskmap_nnodes() == 4 (got %d)",
        n);
    n = taskmap_total_ntasks (map);
    ok (n == 16,
        "taskmap_nnodes() == 16 (got %d)",
        n);
    s = taskmap_encode (map, 0);
    ok (s != NULL,
        "taskmap_encode works");
    is (s, "[[0,4,1,4]]",
        "map = %s",
        s);
    free (s);
    taskmap_destroy (map);
}

void append_cyclic_one ()
{
    int n;
    char *s;
    struct taskmap *map = taskmap_create ();
    if (!map)
        BAIL_OUT ("taskmap_create");

    for (int i = 0; i < 4; i++) {
        ok (taskmap_append (map, 0, 1, 1) == 0,
            "taskmap_append (0, 1, 1)");
    }
    n = taskmap_nnodes (map);
    ok (n == 1,
        "taskmap_nnodes() == 1 (got %d)",
        n);
    n = taskmap_total_ntasks (map);
    ok (n == 4,
        "taskmap_nnodes() == 16 (got %d)",
        n);
    s = taskmap_encode (map, 0);
    ok (s != NULL,
        "taskmap_encode works");
    is (s, "[[0,1,4,1]]",
        "map = %s",
        s);
    free (s);
    taskmap_destroy (map);

}

struct check_test {
    const char *a;
    const char *b;
    int rc;
    const char *errmsg;
};

struct check_test check_tests[] = {
    { "[[0,4,4,1]]", "[[0,4,1,4]]", 0, NULL },
    { "[[0,4,4,1]]", "[[0,4,2,2]]", 0, NULL },
    { "[[0,4,4,1]]", "[[0,4,3,1],[0,4,1,1]]", 0, NULL },
    { "[[0,4,4,1]]", "[[0,2,4,1],[2,1,3,1],[3,1,5,1]]", 0, NULL },
    { "[[0,4,4,1]]", "[[0,4,3,1]]", -1,
      "got 12 total tasks, expected 16" },
    { "[[0,4,4,1]]", "[[0,2,8,1]]", -1,
      "got 2 nodes, expected 4" },
    { NULL, NULL, 0, NULL },
};

static void test_check ()
{
    for (struct check_test *t = &check_tests[0]; t->a != NULL; t++) {
        flux_error_t error;
        struct taskmap *a = taskmap_decode (t->a, &error);
        struct taskmap *b = taskmap_decode (t->b, &error);
        if (!a || !b)
            BAIL_OUT ("taskmap_decode failed: %s", error.text);
        ok (taskmap_check (a, b, &error) == t->rc,
            "taskmap_check ('%s','%s') == %d",
            t->a,
            t->b,
            t->rc);
        if (t->errmsg)
            is (error.text, t->errmsg,
                "got expected error message: %s", error.text);
        taskmap_destroy (a);
        taskmap_destroy (b);
    }
}

void test_deranged (void)
{
    struct taskmap *map;
    flux_error_t error;
    char *s;

    if (!(map = taskmap_decode ("[[0,4,4,1]]", &error)))
        BAIL_OUT ("taskmap_decode: %s", error.text);
    ok ((s = taskmap_encode (map, TASKMAP_ENCODE_RAW_DERANGED)) != NULL,
        "taskmap_encode RAW_DERANGED works");
    is (s, "0,1,2,3;4,5,6,7;8,9,10,11;12,13,14,15",
        "and result is deranged");
    free (s);
    taskmap_destroy (map);
}

struct test_vector raw_tests[] = {
    { "-1",        "error parsing range '-1'" },
    { "1-3;a-b",   "error parsing range 'a-b'" },
    { "1,1",       "range '1' is out of order" },
    { "0-1;1-2",   "duplicate taskid specified: 1" },
    { "5-15;0-10", "duplicate taskids specified: 5-10" },
    { "1",         "missing taskid: 0" },
    { "3-4;0-1",   "missing taskid: 2" },
    { "0-1;10-11", "missing taskids: 2-9" },
    { NULL, NULL },
};

static void test_raw_decode_errors (void)
{
    struct test_vector *t;
    for (t = &raw_tests[0]; t->taskmap != NULL; t++) {
        flux_error_t error;
        ok (taskmap_decode (t->taskmap, &error) == NULL,
            "taskmap_decode (%s) fails",
            t->taskmap);
        is (error.text, t->expected,
            "taskmap_decode: %s",
            error.text);
    }
}

int main (int ac, char **av)
{
    plan (NO_PLAN);
    main_tests ();
    rfc34_tests ();
    pmi_mapping_tests ();
    error_tests ();
    append_tests ();
    append_cyclic_test ();
    append_cyclic_one ();
    test_check ();
    test_deranged ();
    test_raw_decode_errors ();
    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
