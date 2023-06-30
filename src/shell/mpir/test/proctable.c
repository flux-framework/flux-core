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
#include "mpir/proctable.h"

struct entry {
    const char *host;
    const char *executable;
    int taskid;
    int pid;
};

struct entry basic [] = {
    { "foo0", "myapp", 0, 1234 },
    { "foo0", "myapp", 1, 1235 },
    { "foo0", "myapp", 2, 1236 },
    { "foo0", "myapp", 3, 1237 },
    { "foo1", "myapp", 4, 4589 },
    { "foo1", "myapp", 5, 4590 },
    { "foo1", "myapp", 6, 4591 },
    { "foo1", "myapp", 7, 4592 },
    { NULL, NULL, 0, 0 }
};

struct entry leadingzeros [] = {
    { "foo00", "myapp", 0, 1234 },
    { "foo00", "myapp", 1, 1235 },
    { "foo00", "myapp", 2, 1236 },
    { "foo00", "myapp", 3, 1237 },
    { "foo01", "myapp", 4, 4589 },
    { "foo01", "myapp", 5, 4590 },
    { "foo01", "myapp", 6, 4591 },
    { "foo01", "myapp", 7, 4592 },
    { NULL, NULL, 0, 0 }
};

struct entry moreleadingzeros [] = {
    { "foo008", "myapp", 0, 1234 },
    { "foo008", "myapp", 1, 1235 },
    { "foo009", "myapp", 2, 2689 },
    { "foo009", "myapp", 3, 2690 },
    { "foo010", "myapp", 4, 1236 },
    { "foo010", "myapp", 5, 1237 },
    { "foo011", "myapp", 6, 4589 },
    { "foo011", "myapp", 7, 4590 },
    { "foo012", "myapp", 8, 8591 },
    { "foo012", "myapp", 9, 8592 },
    { NULL, NULL, 0, 0 }
};


struct entry append1 [] = {
    { "foo0", "myapp", 0, 1234 },
    { "foo0", "myapp", 1, 1235 },
    { "foo0", "myapp", 2, 1236 },
    { "foo0", "myapp", 3, 1237 },
    { NULL, NULL, 0, 0 },
};

struct entry append2 [] = {
    { "foo1", "myapp", 4, 4589 },
    { "foo1", "myapp", 5, 4590 },
    { "foo1", "myapp", 6, 4591 },
    { "foo1", "myapp", 7, 4592 },
    { NULL, NULL, 0, 0 },
};

static struct proctable * proctable_test_create (struct entry e[])
{
    struct proctable *p = proctable_create ();
    if (!p)
        BAIL_OUT ("proctable_create failed");
    while (e->host) {
        ok (proctable_append_task (p,
                                   e->host,
                                   e->executable,
                                   e->taskid,
                                   e->pid) == 0,
            "proctable_append [%s,%s,%d,%d]",
                                   e->host,
                                   e->executable,
                                   e->taskid,
                                   e->pid);
        ++e;
    }
    return p;
}

static int entry_count (const struct entry *e)
{
    int count = 0;
    while (e->host) {
        count++;
        ++e;
    }
    return count;
}

static void proctable_check (struct proctable *p, const struct entry e[])
{
    MPIR_PROCDESC *table = NULL;
    int size = 0;
    int expected_size = entry_count (e);

    ok ((table = proctable_get_mpir_proctable (p, &size)) != NULL,
        "proctable_get_mpir_proctable");
    ok (size == expected_size,
        "proctable_get_mpir_proctable returned expected size=%d", size);
    ok ((size = proctable_get_size (p)) == expected_size,
        "proctable is of expected size");
    ok (proctable_first_task (p) == e[0].taskid,
        "proctable_first_task works");

    for (int i = 0; i < size; i++) {
        is (table[i].host_name, e[i].host,
            "task%d: host is %s", i, table[i].host_name);
        is (table[i].executable_name, e[i].executable,
            "task%d: executable is %s", i, table[i].executable_name);
        ok (table[i].pid == e[i].pid,
            "task%d: pid is %d", i, table[i].pid);
    }
}

static void dump_json (json_t *o)
{
    char *s = json_dumps (o, JSON_COMPACT);
    diag (s);
    free (s);
}

static void test_proctable (struct entry entries[])
{
    json_t *o = NULL;
    struct proctable *p2 = NULL;
    struct proctable *p = proctable_test_create (entries);
    if (!p)
        BAIL_OUT ("proctable_test_create failed");
    if (!(o = proctable_to_json (p)))
        BAIL_OUT ("proctable_to_json failed");
    dump_json (o);
    if (!(p2 = proctable_from_json (o)))
        BAIL_OUT ("proctable_from_json failed");

    proctable_check (p, entries);
    proctable_check (p2, entries);

    proctable_destroy (p);
    proctable_destroy (p2);
    json_decref (o);
}

static void test_append (void)
{
    struct proctable *p1 = proctable_test_create (append1);
    struct proctable *p2 = proctable_test_create (append2);
    if (!p1 || !p2)
        BAIL_OUT ("proctable_test_create failed");

    ok (proctable_append_proctable_destroy (p1, p2) == 0,
        "proctable_append_proctable_destroy");
    proctable_check (p1, basic);
    proctable_destroy (p1);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    test_proctable (basic);
    test_proctable (leadingzeros);
    test_proctable (moreleadingzeros);
    test_append ();
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
