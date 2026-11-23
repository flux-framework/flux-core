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
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "mpir/proctable.h"

struct entry {
    const char *host;
    int broker_rank;
    const char *executable;
    int taskid;
    int pid;
};

struct entry basic [] = {
    { "foo0", 0, "myapp", 0, 1234 },
    { "foo0", 0, "myapp", 1, 1235 },
    { "foo0", 0, "myapp", 2, 1236 },
    { "foo0", 0, "myapp", 3, 1237 },
    { "foo1", 1, "myapp", 4, 4589 },
    { "foo1", 1, "myapp", 5, 4590 },
    { "foo1", 1, "myapp", 6, 4591 },
    { "foo1", 1, "myapp", 7, 4592 },
    { NULL, 0, NULL, 0, 0 }
};

struct entry leadingzeros [] = {
    { "foo00", 13, "myapp", 0, 1234 },
    { "foo00", 13, "myapp", 1, 1235 },
    { "foo00", 13, "myapp", 2, 1236 },
    { "foo00", 13, "myapp", 3, 1237 },
    { "foo01", 15, "myapp", 4, 4589 },
    { "foo01", 15, "myapp", 5, 4590 },
    { "foo01", 15, "myapp", 6, 4591 },
    { "foo01", 15, "myapp", 7, 4592 },
    { NULL, 0, NULL, 0, 0 }
};

struct entry moreleadingzeros [] = {
    { "foo008", 8, "myapp", 0, 1234 },
    { "foo008", 8, "myapp", 1, 1235 },
    { "foo009", 9, "myapp", 2, 2689 },
    { "foo009", 9, "myapp", 3, 2690 },
    { "foo010", 10, "myapp", 4, 1236 },
    { "foo010", 10, "myapp", 5, 1237 },
    { "foo011", 11, "myapp", 6, 4589 },
    { "foo011", 11, "myapp", 7, 4590 },
    { "foo012", 12, "myapp", 8, 8591 },
    { "foo012", 12, "myapp", 9, 8592 },
    { NULL, 0, NULL, 0, 0 }
};


struct entry append1 [] = {
    { "foo0", 0, "myapp", 0, 1234 },
    { "foo0", 0, "myapp", 1, 1235 },
    { "foo0", 0, "myapp", 2, 1236 },
    { "foo0", 0, "myapp", 3, 1237 },
    { NULL, 0, NULL, 0, 0 },
};

struct entry append2 [] = {
    { "foo1", 1, "myapp", 4, 4589 },
    { "foo1", 1, "myapp", 5, 4590 },
    { "foo1", 1, "myapp", 6, 4591 },
    { "foo1", 1, "myapp", 7, 4592 },
    { NULL, 0, NULL, 0, 0 },
};

static struct proctable * proctable_test_create (struct entry e[])
{
    struct proctable *p = proctable_create ();
    if (!p)
        BAIL_OUT ("proctable_create failed");
    while (e->host) {
        ok (proctable_append_task (p,
                                   e->broker_rank,
                                   e->host,
                                   e->executable,
                                   e->taskid,
                                   e->pid) == 0,
            "proctable_append [%s,%d,%s,%d,%d]",
                                   e->host,
                                   e->broker_rank,
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

    ok ((table = proctable_get_mpir_proctable (p, &size, NULL)) != NULL,
        "proctable_get_mpir_proctable");
    ok (size == expected_size,
        "proctable_get_mpir_proctable returned expected size=%d", size);
    ok ((size = proctable_get_size (p)) == expected_size,
        "proctable is of expected size");
    ok (proctable_first_task (p) == e[0].taskid,
        "proctable_first_task works");

    for (int i = 0; i < size; i++) {
        int broker_rank;
        is (table[i].host_name, e[i].host,
            "task%d: host is %s", i, table[i].host_name);
        is (table[i].executable_name, e[i].executable,
            "task%d: executable is %s", i, table[i].executable_name);
        ok (table[i].pid == e[i].pid,
            "task%d: pid is %d", i, table[i].pid);
        broker_rank = proctable_get_broker_rank (p, i);
        ok (broker_rank == e[i].broker_rank,
            "task%d: broker rank is %d", i, broker_rank);
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

static struct proctable *proctable_from_entries (const struct entry *e)
{
    struct proctable *p = proctable_create ();
    if (!p)
        BAIL_OUT ("proctable_create failed");
    while (e->host) {
        if (proctable_append_task (p,
                                   e->broker_rank,
                                   e->host,
                                   e->executable,
                                   e->taskid,
                                   e->pid) < 0)
            BAIL_OUT ("proctable_append_task failed");
        e++;
    }
    return p;
}

static void test_invalid (const struct entry *e, const char *expected)
{
    flux_error_t error;
    struct proctable *p = proctable_from_entries (e);

    error.text[0] = '\0';
    errno = 0;
    ok (proctable_get_mpir_proctable (p, NULL, &error) == NULL
        && errno == EINVAL,
        "proctable_get_mpir_proctable fails with EINVAL: %s", expected);
    like (error.text, expected,
        "error message is as expected: %s", error.text);
    proctable_destroy (p);

    /*  errp may be NULL */
    p = proctable_from_entries (e);
    ok (proctable_get_mpir_proctable (p, NULL, NULL) == NULL,
        "proctable_get_mpir_proctable fails with errp == NULL");
    proctable_destroy (p);
}

static void test_validation (void)
{
    struct proctable *p;
    flux_error_t error;

    struct entry empty_host[] = {
        { "", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry dash_host[] = {
        { "-foo", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry space_host[] = {
        { "foo 0", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry tab_host[] = {
        { "foo\t0", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry nl_host[] = {
        { "foo\n0", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry ctl_host[] = {
        { "foo\x01", 0, "/bin/true", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry empty_exe[] = {
        { "foo0", 0, "", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry dash_exe[] = {
        { "foo0", 0, "-x", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry nl_exe[] = {
        { "foo0", 0, "/bin/tr\nue", 0, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry range_id[] = {
        { "foo0", 0, "myapp", 1, 1234 }, { NULL, 0, NULL, 0, 0 } };
    struct entry dup_id[] = {
        { "foo0", 0, "myapp", 0, 1234 },
        { "foo0", 0, "myapp", 0, 1235 },
        { NULL, 0, NULL, 0, 0 } };
    struct entry relative[] = {
        { "foo-0.example", 0, "hostname", 0, 1234 },
        { NULL, 0, NULL, 0, 0 } };

    test_invalid (empty_host, "invalid host name.*task 0");
    test_invalid (dash_host, "invalid host name");
    test_invalid (space_host, "invalid host name");
    test_invalid (tab_host, "invalid host name");
    test_invalid (nl_host, "invalid host name");
    test_invalid (ctl_host, "invalid host name");
    test_invalid (empty_exe, "invalid executable name.*task 0");
    test_invalid (dash_exe, "invalid executable name");
    test_invalid (nl_exe, "invalid executable name");
    test_invalid (range_id, "out of range");
    test_invalid (dup_id, "duplicate task id");

    /*  Relative executable names are valid (argv[0] as submitted) */
    p = proctable_from_entries (relative);
    ok (proctable_get_mpir_proctable (p, NULL, &error) != NULL,
        "proctable with relative executable name and dashed host is valid");
    proctable_destroy (p);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    test_validation ();
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
