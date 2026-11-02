/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/taskmap.h>
#include <flux/idset.h>

#include "ccan/str/str.h"

#include "slotmap.h"

#include "src/common/libtap/tap.h"

/* RFC 20 example 2
 */
static const char *R_example2 =
"{\"version\": 1, \"execution\": {"
"  \"R_lite\": ["
"    {\"rank\": \"0-1\","
"     \"children\": {\"core\": \"0-7\", \"gpu\": \"0\"},"
"     \"slots\": {"
"       \"cpu\": {\"count\": 1, \"core\": \"0-3\"},"
"       \"gpu\": {\"count\": 1, \"core\": \"4-7\", \"gpu\": \"0\"}}},"
"    {\"rank\": \"2-3\","
"     \"children\": {\"core\": \"0-3\"},"
"     \"slots\": {\"cpu\": {\"count\": 1, \"core\": \"0-3\"}}}"
"  ],"
"  \"nodelist\": [\"host[0-3]\"],"
"  \"nslots\": 6, \"starttime\": 0, \"expiration\": 0}}";

/* Uneven slots: 3 slots on rank 0, 1 slot on rank 1
 */
static const char *R_uneven =
"{\"version\": 1, \"execution\": {"
"  \"R_lite\": ["
"    {\"rank\": \"0\","
"     \"children\": {\"core\": \"0-5\"},"
"     \"slots\": {\"task\": {\"count\": 3, \"core\": \"0-5\"}}},"
"    {\"rank\": \"1\","
"     \"children\": {\"core\": \"0-1\"},"
"     \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-1\"}}}"
"  ]}}";

static const char *R_noslots =
"{\"version\": 1, \"execution\": {"
"  \"R_lite\": ["
"    {\"rank\": \"0-3\", \"children\": {\"core\": \"0-3\"}},"
"    {\"rank\": \"4-7\", \"children\": {\"core\": \"0\"}}"
"  ]}}";

struct bad_input {
    const char *desc;
    const char *R;
    const char *errmsg;
};

static struct bad_input bad_inputs[] = {
    {
        "slots not in all entries",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-1\"}}},"
        " {\"rank\": \"1\", \"children\": {\"core\": \"0-1\"}}]}}",
        "slots must be present in all R_lite entries or none",
    },
    {
        "slot count of zero",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"count\": 0, \"core\": \"0-1\"}}}]}}",
        "rank 0: slots.task.count must be an integer > 0",
    },
    {
        "slot count missing",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"core\": \"0-1\"}}}]}}",
        "rank 0: slots.task.count must be an integer > 0",
    },
    {
        "core not assigned to a slot",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-3\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-1\"}}}]}}",
        "rank 0: children.core not fully assigned to slots",
    },
    {
        "slot core not in children",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-3\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-4\"}}}]}}",
        "rank 0: slots.task.core not in children.core",
    },
    {
        "core in multiple slots",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-3\"},"
        "  \"slots\": {\"a\": {\"count\": 1, \"core\": \"0-2\"},"
        "             \"b\": {\"count\": 1, \"core\": \"2-3\"}}}]}}",
        "rank 0: slots.b.core overlaps another slot",
    },
    {
        "slot resource type not in children",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-3\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-3\","
        "                        \"gpu\": \"0\"}}}]}}",
        "rank 0: slots.task.gpu not in children",
    },
    {
        "gpu not assigned to a slot",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-3\", \"gpu\": \"0\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-3\"}}}]}}",
        "rank 0: children.gpu not fully assigned to slots",
    },
    {
        "nslots mismatch",
        "{\"version\": 1, \"execution\": {\"nslots\": 3, \"R_lite\": ["
        " {\"rank\": \"0-1\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"0-1\"}}}]}}",
        "sum of slot counts (2) does not equal nslots (3)",
    },
    {
        "empty slots object",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {}}]}}",
        "rank 0: slots must be a non-empty object",
    },
    {
        "rank in multiple entries",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0-1\", \"children\": {\"core\": \"0-1\"}},"
        " {\"rank\": \"1\", \"children\": {\"core\": \"2-3\"}}]}}",
        "rank 1 appears in multiple R_lite entries",
    },
    {
        "invalid slot core idset",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"count\": 1, \"core\": \"foo\"}}}]}}",
        "rank 0: invalid slots.task.core",
    },
    {
        "invalid children idset",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"foo\"}}]}}",
        "rank 0: invalid core or gpu idset",
    },
    {
        "invalid rank idset",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"foo\", \"children\": {\"core\": \"0\"}}]}}",
        "invalid rank idset foo",
    },
    {
        "children missing core",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"gpu\": \"0\"}}]}}",
        "rank 0: Object item not found: core",
    },
    {
        "R_lite is not an array",
        "{\"version\": 1, \"execution\": {\"R_lite\": {}}}",
        "R_lite is not an array",
    },
    {
        "invalid version",
        "{\"version\": 2, \"execution\": {\"R_lite\": []}}",
        "unsupported R version 2",
    },
    {
        "slot count exceeds cores",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\"},"
        "  \"slots\": {\"task\": {\"count\": 3, \"core\": \"0-1\"}}}]}}",
        "rank 0: slots.task.count exceeds its cores",
    },
    {
        "slot with no cores",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0\", \"children\": {\"core\": \"0-1\", \"gpu\": \"0\"},"
        "  \"slots\": {\"cpu\": {\"count\": 1, \"core\": \"0-1\"},"
        "             \"gpu\": {\"count\": 1, \"gpu\": \"0\"}}}]}}",
        "rank 0: slots.gpu.count exceeds its cores",
    },
    {
        "too many slots",
        "{\"version\": 1, \"execution\": {\"R_lite\": ["
        " {\"rank\": \"0-65535\", \"children\": {\"core\": \"0-65535\"},"
        "  \"slots\": {\"task\": {\"count\": 65535,"
        "                        \"core\": \"0-65535\"}}}]}}",
        "too many slots in R",
    },
    { NULL, NULL, NULL },
};

/* Return a NULL terminated array of one label
 */
#define LABELS(l) ((const char *[]){ (l), NULL })

static struct slotmap *create (const char *s, flux_error_t *errp)
{
    json_t *R = json_loads (s, 0, NULL);
    struct slotmap *sm = slotmap_create (R, errp);
    json_decref (R);
    return sm;
}

static int nodes_used (struct slotmap *sm)
{
    int count = 0;
    for (int i = 0; i < slotmap_total_nodes (sm); i++) {
        struct slotmap_rankinfo ri;
        if (slotmap_get_nth (sm, i, &ri) == 0 && ri.ntasks > 0)
            count++;
    }
    return count;
}

static void check_rank (struct slotmap *sm,
                        int nodeid,
                        int ntasks,
                        int nslots,
                        const char *cores,
                        const char *gpus)
{
    struct slotmap_rankinfo ri;
    ok (slotmap_get_nth (sm, nodeid, &ri) == 0,
        "slotmap_get_nth %d works",
        nodeid);
    ok (ri.ntasks == ntasks,
        "nodeid %d: ntasks=%d (expected %d)",
        nodeid,
        ri.ntasks,
        ntasks);
    json_t *slots = slotmap_slots_json (sm, nodeid);
    ok (json_array_size (slots) == nslots,
        "nodeid %d: nslots=%zu (expected %d)",
        nodeid,
        json_array_size (slots),
        nslots);
    json_decref (slots);
    char *c = slotmap_cores (sm, nodeid);
    char *g = slotmap_gpus (sm, nodeid);
    is (c, cores, "nodeid %d: cores=%s", nodeid, c);
    is (g, gpus, "nodeid %d: gpus=%s", nodeid, g);
    free (c);
    free (g);
}

static void check_slot (struct slotmap *sm,
                        int nodeid,
                        int index,
                        const char *label,
                        int ntasks,
                        const char *cores,
                        const char *gpus)
{
    json_t *slots = slotmap_slots_json (sm, nodeid);
    const char *l = NULL;
    const char *c = NULL;
    const char *g = NULL;
    int n = -1;

    ok (slots != NULL
        && json_unpack (json_array_get (slots, index),
                        "{s:s s:i s:s s:s}",
                        "label", &l,
                        "ntasks", &n,
                        "cores", &c,
                        "gpus", &g) == 0,
        "nodeid %d slot %d: unpack works",
        nodeid,
        index);
    is (l, label, "nodeid %d slot %d: label=%s", nodeid, index, l);
    ok (n == ntasks, "nodeid %d slot %d: ntasks=%d", nodeid, index, n);
    is (c, cores, "nodeid %d slot %d: cores=%s", nodeid, index, c);
    is (g, gpus, "nodeid %d slot %d: gpus=%s", nodeid, index, g);
    json_decref (slots);
}

static void test_invalid (void)
{
    flux_error_t error;
    struct slotmap_rankinfo ri;
    struct slotmap *sm;

    errno = 0;
    ok (slotmap_create (NULL, &error) == NULL && errno == EINVAL,
        "slotmap_create (NULL) fails with EINVAL");
    errno = 0;
    ok (create ("{}", &error) == NULL && errno == EINVAL,
        "slotmap_create with invalid R fails with EINVAL");

    ok (slotmap_has_slots (NULL) == false
        && slotmap_total_nodes (NULL) == 0
        && slotmap_total_slots (NULL) == 0,
        "accessors handle NULL slotmap");

    if (!(sm = create (R_noslots, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    errno = 0;
    ok (slotmap_set_slots_even (sm, NULL, 0, 1, 0, &error) < 0
        && errno == EINVAL,
        "slotmap_set_slots_even nslots=0 fails with EINVAL");
    errno = 0;
    ok (slotmap_set_slots_per_resource (sm, NULL, "socket", &error) < 0
        && errno == EINVAL,
        "slotmap_set_slots_per_resource type=socket fails with EINVAL");
    is (error.text,
        "invalid per-resource type socket",
        "got expected error");
    errno = 0;
    ok (slotmap_distribute (sm, NULL, 1, &error) < 0 && errno == ENOENT,
        "slotmap_distribute with no slots fails with ENOENT");
    errno = 0;
    ok (slotmap_get_nth (sm, 8, &ri) < 0 && errno == EINVAL,
        "slotmap_get_nth with invalid nodeid fails with EINVAL");
    errno = 0;
    ok (slotmap_nodeid (sm, 42) < 0 && errno == ENOENT,
        "slotmap_nodeid with unknown rank fails with ENOENT");
    ok (slotmap_nodeid (sm, 7) == 7,
        "slotmap_nodeid works");
    errno = 0;
    ok (slotmap_slots_json (sm, -1) == NULL && errno == EINVAL,
        "slotmap_slots_json with invalid nodeid fails with EINVAL");
    errno = 0;
    ok (slotmap_cores (sm, 8) == NULL && errno == EINVAL,
        "slotmap_cores with invalid nodeid fails with EINVAL");
    errno = 0;
    ok (slotmap_gpus (NULL, 0) == NULL && errno == EINVAL,
        "slotmap_gpus (NULL) fails with EINVAL");
    errno = 0;
    ok (slotmap_update_map (sm, NULL) < 0 && errno == EINVAL,
        "slotmap_update_map map=NULL fails with EINVAL");
    errno = 0;
    ok (slotmap_normalize (NULL, 1, 0) < 0 && errno == EINVAL,
        "slotmap_normalize (NULL) fails with EINVAL");
    slotmap_destroy (sm);
}

static void test_bad_slots (void)
{
    for (int i = 0; bad_inputs[i].desc; i++) {
        flux_error_t error;
        struct slotmap *sm;

        error.text[0] = '\0';
        sm = create (bad_inputs[i].R, &error);
        ok (sm == NULL, "%s: slotmap_create fails", bad_inputs[i].desc);
        is (error.text,
            bad_inputs[i].errmsg,
            "%s: got expected error",
            bad_inputs[i].desc);
        slotmap_destroy (sm);
    }
}

static void test_example2 (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_example2, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    ok (slotmap_has_slots (sm), "slotmap_has_slots is true");
    ok (slotmap_total_nodes (sm) == 4, "total nodes is 4");
    ok (slotmap_total_cores (sm) == 24, "total cores is 24");
    ok (slotmap_total_gpus (sm) == 2, "total gpus is 2");
    ok (slotmap_total_slots (sm) == 6, "total slots is 6");
    ok (slotmap_count_slots (sm, NULL) == 6, "6 slots in total");
    ok (slotmap_count_slots (sm, "cpu") == 4, "4 slots with label cpu");
    ok (slotmap_count_slots (sm, "gpu") == 2, "2 slots with label gpu");
    ok (slotmap_count_slots (sm, "foo") == 0, "0 slots with label foo");

    ok (slotmap_distribute (sm, NULL, 6, &error) == 0,
        "slotmap_distribute 6 tasks to all slots works");
    ok (slotmap_total_ntasks (sm) == 6, "total ntasks is 6");
    ok (nodes_used (sm) == 4, "4 nodes used");
    check_rank (sm, 0, 2, 2, "0-7", "0");
    check_rank (sm, 1, 2, 2, "0-7", "0");
    check_rank (sm, 2, 1, 1, "0-3", "");
    check_rank (sm, 3, 1, 1, "0-3", "");
    check_slot (sm, 0, 0, "cpu", 1, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 1, "4-7", "0");

    ok (slotmap_distribute (sm, LABELS ("gpu"), 2, &error) == 0,
        "slotmap_distribute 2 tasks to gpu slots works");
    ok (nodes_used (sm) == 2, "2 nodes used");
    check_rank (sm, 0, 1, 2, "0-7", "0");
    check_rank (sm, 1, 1, 2, "0-7", "0");
    check_rank (sm, 2, 0, 1, "0-3", "");
    check_slot (sm, 0, 0, "cpu", 0, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 1, "4-7", "0");

    ok (slotmap_normalize (sm, 0, 0) == 0, "slotmap_normalize works");
    check_rank (sm, 0, 1, 2, "4-7", "0");
    check_rank (sm, 2, 0, 1, "", "");

    ok (slotmap_distribute (sm, LABELS ("foo"), 6, &error) == 0,
        "slotmap_distribute to unknown label uses all slots");
    check_slot (sm, 0, 0, "cpu", 1, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 1, "4-7", "0");

    slotmap_destroy (sm);
}

/* RFC 20 example 2 with the slots keys of rank 0-1 in reverse order
 */
static const char *R_example2_reversed =
"{\"version\": 1, \"execution\": {"
"  \"R_lite\": ["
"    {\"rank\": \"0-1\","
"     \"children\": {\"core\": \"0-7\", \"gpu\": \"0\"},"
"     \"slots\": {"
"       \"gpu\": {\"count\": 1, \"core\": \"4-7\", \"gpu\": \"0\"},"
"       \"cpu\": {\"count\": 1, \"core\": \"0-3\"}}},"
"    {\"rank\": \"2-3\","
"     \"children\": {\"core\": \"0-3\"},"
"     \"slots\": {\"cpu\": {\"count\": 1, \"core\": \"0-3\"}}}"
"  ],"
"  \"nodelist\": [\"host[0-3]\"],"
"  \"nslots\": 6, \"starttime\": 0, \"expiration\": 0}}";

static void test_slot_order (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_example2_reversed, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);
    check_slot (sm, 0, 0, "cpu", 0, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 0, "4-7", "0");
    ok (slotmap_distribute (sm, NULL, 2, &error) == 0,
        "slotmap_distribute 2 tasks works");
    check_slot (sm, 0, 0, "cpu", 1, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 0, "4-7", "0");
    slotmap_destroy (sm);
}

/* Distribute tasks over slots with any of several labels, e.g. for the
 * labels of RFC 14 "or" slots, where the scheduler chose which labels
 * to assign and other labels in the jobspec are unused.
 */
static void test_labels (void)
{
    flux_error_t error;
    struct slotmap *sm;
    const char *labels[] = { "gpu", "cpu", "unused", NULL };
    const char *unused[] = { "foo", "bar", NULL };

    if (!(sm = create (R_example2, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    ok (slotmap_count_slots_labels (sm, labels) == 6,
        "6 slots with labels gpu, cpu, or unused");
    ok (slotmap_count_slots_labels (sm, unused) == 0,
        "0 slots with labels foo or bar");
    ok (slotmap_count_slots_labels (sm, NULL) == 6,
        "6 slots with NULL labels");

    ok (slotmap_distribute (sm, labels, 6, &error) == 0,
        "slotmap_distribute to multiple labels works");
    check_rank (sm, 0, 2, 2, "0-7", "0");
    check_rank (sm, 2, 1, 1, "0-3", "");
    check_slot (sm, 0, 0, "cpu", 1, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 1, "4-7", "0");

    ok (slotmap_distribute (sm, LABELS ("cpu"), 4, &error) == 0,
        "slotmap_distribute to one of the labels works");
    check_slot (sm, 0, 0, "cpu", 1, "0-3", "");
    check_slot (sm, 0, 1, "gpu", 0, "4-7", "0");

    ok (slotmap_distribute (sm, unused, 6, &error) == 0,
        "slotmap_distribute with no matching labels uses all slots");
    check_slot (sm, 0, 1, "gpu", 1, "4-7", "0");

    slotmap_destroy (sm);
}

static void test_uneven (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_uneven, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    ok (slotmap_total_slots (sm) == 4,
        "total slots is 4 (no nslots in R)");

    /* Slots with a count > 1 split the cores evenly
     */
    check_slot (sm, 0, 0, "task", 0, "0-1", "");
    check_slot (sm, 0, 1, "task", 0, "2-3", "");
    check_slot (sm, 0, 2, "task", 0, "4-5", "");

    ok (slotmap_distribute (sm, NULL, 4, &error) == 0,
        "slotmap_distribute 4 tasks works");
    check_rank (sm, 0, 3, 3, "0-5", "");
    check_rank (sm, 1, 1, 1, "0-1", "");

    /* Fewer tasks than slots: round-robin, most slots first.
     */
    ok (slotmap_distribute (sm, NULL, 3, &error) == 0,
        "slotmap_distribute 3 tasks works");
    check_rank (sm, 0, 2, 3, "0-5", "");
    check_rank (sm, 1, 1, 1, "0-1", "");

    ok (slotmap_normalize (sm, 0, 0) == 0, "slotmap_normalize works");
    check_rank (sm, 0, 2, 3, "0-3", "");
    check_rank (sm, 1, 1, 1, "0-1", "");

    /* Oversubscribe slots
     */
    ok (slotmap_distribute (sm, NULL, 6, &error) == 0,
        "slotmap_distribute 6 tasks works");
    check_rank (sm, 0, 4, 3, "0-3", "");
    check_rank (sm, 1, 2, 1, "0-1", "");
    check_slot (sm, 0, 0, "task", 2, "0-1", "");
    check_slot (sm, 0, 1, "task", 1, "2-3", "");

    slotmap_destroy (sm);
}

static void test_even (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_noslots, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    ok (!slotmap_has_slots (sm), "slotmap_has_slots is false");
    ok (slotmap_total_slots (sm) == 0, "total slots is 0");
    ok (slotmap_count_slots (sm, NULL) == 0, "no slots exist yet");

    ok (slotmap_set_slots_even (sm, "task", 9, 0, 0, &error) == 0,
        "slotmap_set_slots_even 9 slots works");
    ok (!slotmap_has_slots (sm), "slotmap_has_slots is still false");
    ok (slotmap_count_slots (sm, "task") == 9, "9 slots created");
    ok (slotmap_distribute (sm, LABELS ("task"), 9, &error) == 0,
        "slotmap_distribute 9 tasks works");
    check_rank (sm, 0, 2, 2, "0-3", "");
    check_rank (sm, 1, 1, 1, "0-3", "");
    check_rank (sm, 4, 1, 1, "0", "");
    /* Slot size is limited by the smallest rank (1 core)
     */
    check_slot (sm, 0, 0, "task", 1, "0", "");
    check_slot (sm, 0, 1, "task", 1, "1", "");

    ok (slotmap_set_slots_even (sm, NULL, 4, 4, 0, &error) == 0,
        "slotmap_set_slots_even 4 slots of 4 cores works");
    ok (slotmap_distribute (sm, NULL, 4, &error) == 0,
        "slotmap_distribute 4 tasks works");
    ok (nodes_used (sm) == 4, "4 nodes used");
    check_rank (sm, 3, 1, 1, "0-3", "");
    check_rank (sm, 4, 0, 0, "0", "");

    ok (slotmap_set_slots_per_resource (sm, NULL, "core", &error) == 0,
        "slotmap_set_slots_per_resource core works");
    ok (slotmap_count_slots (sm, NULL) == 20, "20 slots created");
    ok (slotmap_distribute (sm, NULL, 40, &error) == 0,
        "slotmap_distribute 40 tasks works");
    check_rank (sm, 0, 8, 4, "0-3", "");
    check_rank (sm, 7, 2, 1, "0", "");

    ok (slotmap_set_slots_per_resource (sm, NULL, "node", &error) == 0,
        "slotmap_set_slots_per_resource node works");
    ok (slotmap_count_slots (sm, NULL) == 8, "8 slots created");
    ok (slotmap_distribute (sm, NULL, 8, &error) == 0,
        "slotmap_distribute 8 tasks works");
    check_rank (sm, 0, 1, 1, "0-3", "");
    check_rank (sm, 7, 1, 1, "0", "");

    /* Without slots in R, normalize keeps cores_per_task cores per task
     */
    ok (slotmap_distribute (sm, NULL, 4, &error) == 0,
        "slotmap_distribute 4 tasks works");
    ok (slotmap_normalize (sm, 2, 0) == 0,
        "slotmap_normalize 2 cores per task works");
    check_rank (sm, 0, 1, 1, "0-1", "");
    check_rank (sm, 4, 0, 1, "", "");

    slotmap_destroy (sm);
}

static void test_update_map (void)
{
    flux_error_t error;
    struct slotmap *sm;
    struct taskmap *map;

    if (!(sm = create (R_uneven, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);
    if (slotmap_distribute (sm, NULL, 4, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);

    if (!(map = taskmap_decode ("[[0,1,1,1],[1,1,3,1]]", NULL)))
        BAIL_OUT ("taskmap_decode failed");
    ok (slotmap_update_map (sm, map) == 0, "slotmap_update_map works");
    ok (slotmap_total_ntasks (sm) == 4, "total ntasks is 4");
    check_rank (sm, 0, 1, 3, "0-5", "");
    check_rank (sm, 1, 3, 1, "0-1", "");
    check_slot (sm, 0, 0, "task", 1, "0-1", "");
    check_slot (sm, 0, 1, "task", 0, "2-3", "");
    check_slot (sm, 1, 0, "task", 3, "0-1", "");

    errno = 0;
    ok (slotmap_update_map (sm, NULL) < 0 && errno == EINVAL,
        "slotmap_update_map (NULL) fails with EINVAL");

    taskmap_destroy (map);
    slotmap_destroy (sm);
}

/* Create a block taskmap from the current task counts in `sm`
 */
static struct taskmap *map_from_slotmap (struct slotmap *sm)
{
    struct taskmap *map = taskmap_create ();
    if (!map)
        BAIL_OUT ("taskmap_create failed");
    for (int i = 0; i < slotmap_total_nodes (sm); i++) {
        struct slotmap_rankinfo ri;
        if (slotmap_get_nth (sm, i, &ri) < 0)
            BAIL_OUT ("slotmap_get_nth %d failed", i);
        if (ri.ntasks > 0 && taskmap_append (map, i, 1, ri.ntasks) < 0)
            BAIL_OUT ("failed to create taskmap for nodeid %d", i);
    }
    return map;
}

static void update_map (struct slotmap *sm, const char *s)
{
    struct taskmap *map;
    flux_error_t error;

    if (s) {
        if (!(map = taskmap_decode (s, &error)))
            BAIL_OUT ("taskmap_decode %s: %s", s, error.text);
    }
    else
        map = map_from_slotmap (sm);
    ok (slotmap_update_map (sm, map) == 0,
        "slotmap_update_map %s works",
        s ? s : "(block)");
    taskmap_destroy (map);
}

/* Return true if slot `id` of some rank in slotmap_slots_json() has task
 * `taskid` and the label and cores of task slot `o`.
 */
static bool rank_slot_has_task (struct slotmap *sm,
                                int id,
                                int taskid,
                                json_t *o)
{
    bool found = false;

    for (int i = 0; i < slotmap_total_nodes (sm) && !found; i++) {
        json_t *slots = slotmap_slots_json (sm, i);
        json_t *slot = json_array_get (slots, id);
        const char *taskids;
        struct idset *ids;

        if (slot
            && json_unpack (slot, "{s:s}", "taskids", &taskids) == 0
            && (ids = idset_decode (taskids))) {
            found = idset_test (ids, taskid)
                    && json_equal (json_object_get (slot, "label"),
                                   json_object_get (o, "label"))
                    && json_equal (json_object_get (slot, "cores"),
                                   json_object_get (o, "cores"));
            idset_destroy (ids);
        }
        json_decref (slots);
    }
    return found;
}

static void check_task (struct slotmap *sm,
                        int taskid,
                        const char *label,
                        int ntasks,
                        int index,
                        const char *cores,
                        const char *gpus)
{
    json_t *o = slotmap_task_slot_json (sm, taskid);
    const char *l = "";
    const char *c = "";
    const char *g = "";
    int n = -1;
    int i = -1;
    int id = -1;

    if (!o
        || json_unpack (o,
                        "{s:i s:s s:i s:i s:s s:s}",
                        "id", &id,
                        "label", &l,
                        "ntasks", &n,
                        "task_index", &i,
                        "cores", &c,
                        "gpus", &g) < 0) {
        fail ("task %d: slotmap_task_slot_json failed", taskid);
        json_decref (o);
        return;
    }
    ok (streq (l, label)
        && n == ntasks
        && i == index
        && streq (c, cores)
        && streq (g, gpus),
        "task %d: label=%s ntasks=%d task_index=%d cores=%s gpus=%s",
        taskid,
        l,
        n,
        i,
        c,
        g);
    ok (rank_slot_has_task (sm, id, taskid, o),
        "task %d: rank slot %d has the task",
        taskid,
        id);
    json_decref (o);
}

static void check_slot_taskids (struct slotmap *sm,
                                int nodeid,
                                int index,
                                const char *expected)
{
    json_t *slots = slotmap_slots_json (sm, nodeid);
    const char *taskids = NULL;

    ok (slots != NULL
        && json_unpack (json_array_get (slots, index),
                        "{s:s}",
                        "taskids", &taskids) == 0,
        "nodeid %d slot %d: unpack taskids works",
        nodeid,
        index);
    is (taskids,
        expected,
        "nodeid %d slot %d: taskids=%s",
        nodeid,
        index,
        taskids);
    json_decref (slots);
}

static void test_task_slot_invalid (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_example2, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    errno = 0;
    ok (slotmap_task_slot_json (NULL, 0) == NULL && errno == EINVAL,
        "slotmap_task_slot_json sm=NULL fails with EINVAL");
    errno = 0;
    ok (slotmap_task_slot_json (sm, -1) == NULL && errno == EINVAL,
        "slotmap_task_slot_json taskid=-1 fails with EINVAL");
    errno = 0;
    ok (slotmap_task_slot_json (sm, 0) == NULL && errno == ENOENT,
        "slotmap_task_slot_json before slotmap_update_map fails with ENOENT");
    check_slot_taskids (sm, 0, 0, "");

    if (slotmap_distribute (sm, NULL, 6, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);
    update_map (sm, NULL);
    errno = 0;
    ok (slotmap_task_slot_json (sm, 6) == NULL && errno == ENOENT,
        "slotmap_task_slot_json with unknown taskid fails with ENOENT");

    slotmap_destroy (sm);
}

static void test_task_slot_example2 (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_example2, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    /* All slots: one task per slot
     */
    if (slotmap_distribute (sm, NULL, 6, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);
    update_map (sm, NULL);
    check_task (sm, 0, "cpu", 1, 0, "0-3", "");
    check_task (sm, 1, "gpu", 1, 0, "4-7", "0");
    check_task (sm, 2, "cpu", 1, 0, "0-3", "");
    check_task (sm, 3, "gpu", 1, 0, "4-7", "0");
    check_task (sm, 4, "cpu", 1, 0, "0-3", "");
    check_task (sm, 5, "cpu", 1, 0, "0-3", "");
    check_slot_taskids (sm, 0, 0, "0");
    check_slot_taskids (sm, 0, 1, "1");
    check_slot_taskids (sm, 3, 0, "5");

    /* Only gpu slots: cpu slots get no tasks
     */
    if (slotmap_distribute (sm, LABELS ("gpu"), 2, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);
    update_map (sm, NULL);
    check_task (sm, 0, "gpu", 1, 0, "4-7", "0");
    check_task (sm, 1, "gpu", 1, 0, "4-7", "0");
    check_slot_taskids (sm, 0, 0, "");
    check_slot_taskids (sm, 0, 1, "0");
    check_slot_taskids (sm, 2, 0, "");

    /* A taskmap that places a task on a rank with no gpu slots falls
     * back to the other slots on that rank.
     */
    update_map (sm, "[[0,1,1,1],[2,1,1,1]]");
    check_task (sm, 0, "gpu", 1, 0, "4-7", "0");
    check_task (sm, 1, "cpu", 1, 0, "0-3", "");

    slotmap_destroy (sm);
}

static void test_task_slot_taskmap (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_uneven, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);
    if (slotmap_distribute (sm, NULL, 4, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);

    /* Block taskmap
     */
    update_map (sm, NULL);
    check_task (sm, 0, "task", 1, 0, "0-1", "");
    check_task (sm, 1, "task", 1, 0, "2-3", "");
    check_task (sm, 2, "task", 1, 0, "4-5", "");
    check_task (sm, 3, "task", 1, 0, "0-1", "");

    /* Non-contiguous task ids on a rank are assigned in task id order
     */
    update_map (sm, "[[0,1,1,1],[1,1,1,1],[0,1,2,1]]");
    check_task (sm, 0, "task", 1, 0, "0-1", "");
    check_task (sm, 1, "task", 1, 0, "0-1", "");
    check_task (sm, 2, "task", 1, 0, "2-3", "");
    check_task (sm, 3, "task", 1, 0, "4-5", "");
    check_slot_taskids (sm, 0, 1, "2");

    /* Fewer tasks than slots on a rank leaves trailing slots empty
     */
    update_map (sm, "[[0,1,1,1],[1,1,3,1]]");
    check_task (sm, 0, "task", 1, 0, "0-1", "");
    check_slot_taskids (sm, 0, 1, "");
    check_task (sm, 1, "task", 3, 0, "0-1", "");
    check_task (sm, 2, "task", 3, 1, "0-1", "");
    check_task (sm, 3, "task", 3, 2, "0-1", "");

    /* Oversubscribed slots are filled round-robin
     */
    if (slotmap_distribute (sm, NULL, 6, &error) < 0)
        BAIL_OUT ("slotmap_distribute failed: %s", error.text);
    update_map (sm, NULL);
    check_slot_taskids (sm, 0, 0, "0,3");
    check_slot_taskids (sm, 0, 1, "1");
    check_slot_taskids (sm, 0, 2, "2");
    check_slot_taskids (sm, 1, 0, "4-5");
    check_task (sm, 0, "task", 2, 0, "0-1", "");
    check_task (sm, 3, "task", 2, 1, "0-1", "");
    check_task (sm, 5, "task", 2, 1, "0-1", "");

    /* Task counts from slotmap_distribute() match the assigned task ids
     * for the block taskmap.
     */
    for (int i = 0; i < slotmap_total_nodes (sm); i++) {
        json_t *slots = slotmap_slots_json (sm, i);
        size_t index;
        json_t *o;
        json_array_foreach (slots, index, o) {
            const char *taskids;
            int ntasks;
            struct idset *ids = NULL;
            if (json_unpack (o,
                             "{s:i s:s}",
                             "ntasks", &ntasks,
                             "taskids", &taskids) < 0
                || !(ids = idset_decode (taskids)))
                BAIL_OUT ("failed to unpack slot");
            ok (idset_count (ids) == ntasks,
                "nodeid %d slot %zu: %d taskids for %d tasks",
                i,
                index,
                (int) idset_count (ids),
                ntasks);
            idset_destroy (ids);
        }
        json_decref (slots);
    }
    slotmap_destroy (sm);
}

static void test_task_slot_noslots (void)
{
    flux_error_t error;
    struct slotmap *sm;

    if (!(sm = create (R_noslots, &error)))
        BAIL_OUT ("slotmap_create failed: %s", error.text);

    /* Task to slot mapping also works for slots created by the shell
     */
    if (slotmap_set_slots_per_resource (sm, NULL, "node", &error) < 0
        || slotmap_distribute (sm, NULL, 16, &error) < 0)
        BAIL_OUT ("failed to distribute tasks: %s", error.text);
    update_map (sm, NULL);
    check_task (sm, 0, "task", 2, 0, "0-3", "");
    check_task (sm, 1, "task", 2, 1, "0-3", "");
    check_task (sm, 15, "task", 2, 1, "0", "");

    /* Replacing slots clears task id assignments
     */
    if (slotmap_set_slots_even (sm, NULL, 4, 1, 0, &error) < 0)
        BAIL_OUT ("slotmap_set_slots_even failed: %s", error.text);
    errno = 0;
    ok (slotmap_task_slot_json (sm, 0) == NULL,
        "slotmap_task_slot_json fails after slots are replaced");

    slotmap_destroy (sm);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    test_invalid ();
    test_bad_slots ();
    test_example2 ();
    test_slot_order ();
    test_labels ();
    test_uneven ();
    test_even ();
    test_update_map ();
    test_task_slot_invalid ();
    test_task_slot_example2 ();
    test_task_slot_taskmap ();
    test_task_slot_noslots ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
