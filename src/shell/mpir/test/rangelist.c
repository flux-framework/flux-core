/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"

#include "mpir/rangelist.h"

void rangelist_diag (struct rangelist *rl)
{
    char *s;
    json_t *o = rangelist_to_json (rl);
    if (!o)
        BAIL_OUT ("rangelist_to_json failed");
    s = json_dumps (o, JSON_COMPACT|JSON_ENCODE_ANY);
    diag (s);
    free (s);
    json_decref (o);
}

void test_rangelist_append (void)
{
    struct rangelist *rl = rangelist_create ();
    struct rangelist *rl2 = rangelist_create ();
    ok (rangelist_append (rl, -1) == 0,
        "rangelist_append -1");
    ok (rangelist_append (rl2, -1) == 0,
        "rangelist_append -1");
    ok (rangelist_append_list (rl, rl2) == 0,
        "rangelist_append_list");
    ok (rangelist_size (rl) == 2,
        "rangelist_size == 2");
    ok (rangelist_first (rl) == -1 && rangelist_next (rl) == -1,
        "rangelist contents are expected");
    rangelist_diag (rl);
    rangelist_destroy (rl);
    rangelist_destroy (rl2);
}

void test_rangelist_append_dups (void)
{
    struct rangelist *rl = rangelist_create ();
    struct rangelist *rl2 = rangelist_create ();
    ok (rangelist_append (rl, 18) == 0,
        "rangelist_append 18");
    ok (rangelist_append (rl, 18) == 0,
        "rangelist_append 18");
    ok (rangelist_append (rl2, 19) == 0,
        "rangelist_append 19");
    ok (rangelist_append (rl2, 19) == 0,
        "rangelist_append 19");
    ok (rangelist_append_list (rl, rl2) == 0,
        "rangelist_append_list");
    ok (rangelist_size (rl) == 4,
        "rangelist_size == 4");
    ok (rangelist_first (rl) == 18 && rangelist_next (rl) == 18 &&
        rangelist_next (rl) == 19 && rangelist_next (rl) == 19,
        "rangelist contents are expected");
    rangelist_diag (rl);
    rangelist_destroy (rl);
    rangelist_destroy (rl2);
}

void test_rangelist_append_range_dups (void)
{
    struct rangelist *rl = rangelist_create ();
    ok (rangelist_append (rl, 1) == 0
        && rangelist_append (rl, 1) == 0
        && rangelist_append (rl, 1) == 0
        && rangelist_append (rl, 1) == 0,
        "rangelist_append 4 1s");
    ok (rangelist_append (rl, 2) == 0
        && rangelist_append (rl, 2) == 0
        && rangelist_append (rl, 2) == 0
        && rangelist_append (rl, 2) == 0,
        "rangelist_append 4 2s");
    ok (rangelist_append (rl, 3) == 0
        && rangelist_append (rl, 3) == 0
        && rangelist_append (rl, 3) == 0
        && rangelist_append (rl, 3) == 0,
        "rangelist_append 4 3s");
    ok (rangelist_append (rl, 4) == 0
        && rangelist_append (rl, 4) == 0
        && rangelist_append (rl, 4) == 0
        && rangelist_append (rl, 4) == 0,
        "rangelist_append 4 4s");
    rangelist_diag (rl);

    json_t *o;
    ok ((o = rangelist_to_json (rl)) != NULL,
        "rangelist_to_json");

    struct rangelist *rl2 = rangelist_from_json (o);
    ok (rl2 != NULL,
        "rangelist_from_json works size=%d",
        rangelist_size (rl2));
    ok (rangelist_size (rl2) == rangelist_size (rl),
        "rangelist_size matches");

    json_decref (o);
    rangelist_destroy (rl);
    rangelist_destroy (rl2);
}

void test_rangelist_basic (void)
{
    struct rangelist *rl2;
    struct rangelist *rl = rangelist_create ();
    if (!rl)
        BAIL_OUT ("Failed to create a rangelist!");
    ok (rangelist_append (rl, 1234) == 0, "rangelist_append 1234");
    ok (rangelist_append (rl, 1235) == 0, "rangelist_append 1235");
    ok (rangelist_append (rl, 1236) == 0, "rangelist_append 1236");
    ok (rangelist_append (rl, 1237) == 0, "rangelist_append 1237");
    ok (rangelist_append (rl, 1411) == 0, "rangelist_append 1411");
    ok (rangelist_append (rl, 1500) == 0, "rangelist_append 1500");
    ok (rangelist_append (rl, 1500) == 0, "rangelist_append 1500");
    ok (rangelist_append (rl, 1500) == 0, "rangelist_append 1500");
    ok (rangelist_append (rl, 1600) == 0, "rangelist_append 1600");
    ok (rangelist_append (rl, 1599) == 0, "rangelist_append 1599");
    ok (rangelist_size (rl) == 10, "rangelist_size is now 10");
    ok (rangelist_first (rl) == 1234,
        "rangelist_first returns first value");
    ok (rangelist_next (rl) == 1235,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1236,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1237,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1411,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1500,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1500,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1500,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1600,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == 1599,
        "rangelist_next returns next value");
    ok (rangelist_next (rl) == RANGELIST_END,
        "rangelist_next returns END value");

    json_t *o;
    ok ((o = rangelist_to_json (rl)) != NULL,
        "rangelist_to_json");

    rangelist_diag (rl);

    if (!(rl2 = rangelist_from_json (o)))
        BAIL_OUT ("rangelist_from_json failed");

    json_decref (o);

    ok (rangelist_size (rl2) == 10,
        "rangelist_size is now 10 (got %d)", rangelist_size (rl2));
    ok (rangelist_first (rl2) == 1234,
        "rangelist_first returns 1234");
    ok (rangelist_next (rl2) == 1235,
        "rangelist_next returns 1235");
    ok (rangelist_next (rl2) == 1236,
        "rangelist_next returns 1236");
    ok (rangelist_next (rl2) == 1237,
        "rangelist_next returns 1237");
    ok (rangelist_next (rl2) == 1411,
        "rangelist_next returns 1411");
    ok (rangelist_next (rl2) == 1500,
        "rangelist_next returns 1500");
    ok (rangelist_next (rl2) == 1500,
        "rangelist_next returns 1500");
    ok (rangelist_next (rl2) == 1500,
        "rangelist_next returns 1500");
    ok (rangelist_next (rl2) == 1600,
        "rangelist_next returns 1600");
    ok (rangelist_next (rl2) == 1599,
        "rangelist_next returns 1599");

    ok (rangelist_next (rl2) == RANGELIST_END,
        "rangelist_next returns END value");

    rangelist_destroy (rl);
    rangelist_destroy (rl2);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    test_rangelist_basic ();
    test_rangelist_append ();
    test_rangelist_append_dups ();
    test_rangelist_append_range_dups ();
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
