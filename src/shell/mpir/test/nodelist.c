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
#include "mpir/nodelist.h"

static const char *test0 [] = {
    "foo0",
    "foo0",
    "foo0",
    "foo0",
    "foo1",
    "foo3",
    "foo4",
    "foo5",
    "foo6",
    "foo7",
    "foo8",
    "foo9",
    "foo10",
    "bar",
    "baz",
    "baz",
    NULL
};

static const char *test1 [] = {
    "fluke18",
    "fluke18",
    "fluke19",
    "fluke19",
    NULL,
};

static const char *test2 [] = {
    "test01",
    "test02",
    "test09",
    "test0201",
    "test0202",
    "test0203",
    "test1200",
    "test1201",
    "test1202",
    NULL
};

static const char *test3 [] = {
    "foo008",
    "foo008",
    "foo009",
    "foo009",
    "foo010",
    "foo010",
    "foo011",
    "foo011",
    NULL
};


struct nodelist * nodelist_from_array (const char *names[])
{
    struct nodelist *nl = nodelist_create ();

    if (!nl)
        BAIL_OUT ("Failed to create nodelist!");

    for (const char **s = names; *s != NULL; s++)
        ok (nodelist_append (nl, *s) == 0,
            "nodelist_append (%s)", *s);

    return nl;
}

void check_nodelist_from_array (struct nodelist *nl, const char *names[])
{
    int i = 0;
    char *host = nodelist_first (nl);
    while (names[i]) {
        is (host, names[i],
            "Got expected hostname in index %d: %s", i,  host);
        free (host);
        host = nodelist_next (nl);
        i++;
    }
}

static void do_test (const char *names[])
{
    json_t *o;
    struct nodelist *nl = nodelist_from_array (names);
    check_nodelist_from_array (nl, names);
    o = nodelist_to_json (nl);
    char *s = json_dumps (o, JSON_COMPACT|JSON_ENCODE_ANY);
    diag (s);
    free (s);

    struct nodelist *nl2 = nodelist_from_json (o);

    check_nodelist_from_array (nl2, names);

    json_decref (o);
    nodelist_destroy (nl);
    nodelist_destroy (nl2);
}

static void test_append (void)
{
    const char *a[] = { "foo10", "foo10", NULL };
    const char *b[] = { "foo11", "foo11", NULL };
    const char *result[] = { "foo10", "foo10", "foo11", "foo11", NULL };
    struct nodelist *nl = nodelist_from_array (a);
    struct nodelist *nl2 = nodelist_from_array (b);
    if (!nl || !nl2)
        BAIL_OUT ("test_append: failed to create nodelists");
    nodelist_append_list_destroy (nl, nl2);
    check_nodelist_from_array (nl, result);
    nodelist_destroy (nl);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    do_test (test0);
    do_test (test1);
    do_test (test2);
    do_test (test3);
    test_append ();
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
