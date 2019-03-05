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
#include <jansson.h>
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/treeobj.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/util.h"

struct jobkey_input {
    flux_jobid_t id;
    bool active;
    const char *key;
    const char *expected;
};

struct jobkey_input jobkeytab[] = {
    { 1, true, NULL,            "job.active.0000.0000.0000.0001" },
    { 1, false, NULL,           "job.inactive.0000.0000.0000.0001" },
    { 2, true, "foo",           "job.active.0000.0000.0000.0002.foo" },
    { 2, false, "foo",          "job.inactive.0000.0000.0000.0002.foo" },
    { 3, true, "a.b.c",         "job.active.0000.0000.0000.0003.a.b.c" },
    { 0xdeadbeef, true, NULL,   "job.active.0000.0000.dead.beef" },

    /* expected failure: overflow */
    { 4, true, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", NULL },

    { 0, false, NULL, NULL },
};
bool is_jobkeytab_end (struct jobkey_input *try)
{
    if (try->id == 0 && try->active == false && !try->key && !try->expected)
        return true;
    return false;
}

void check_one_jobkey (struct jobkey_input *try)
{
    char path[64];
    int len;
    bool valid = false;

    memset (path, 0, sizeof (path));
    len = util_jobkey (path, sizeof (path), try->active, try->id, try->key);

    if (try->expected) {
        if (len >= 0 && len == strlen (try->expected)
                     && !strcmp (path, try->expected))
            valid = true;
    }
    else { // expected failure
        if (len < 0)
            valid = true;
    }
    ok (valid == true,
        "util_jobkey id=%llu active=%s key=%s %s",
        (unsigned long long)try->id,
        try->active ? "true" : "false",
        try->key ? try->key : "NULL",
        try->expected ? "works" : "fails");

    if (!valid)
        diag ("jobkey: %s", path);
}

void check_jobkey (void)
{
    int i;
    for (i = 0; !is_jobkeytab_end (&jobkeytab[i]); i++)
        check_one_jobkey (&jobkeytab[i]);
}

struct context_input {
    const char *context;
    const char *key;
    int intval;
    const char *strval;
    const char *note;
    int rc;
    int errnum;
};

struct context_input contexttab[] = {
    /* Integer */
    { "foo=42",                         "foo", 42, NULL, NULL, 0, 0 },
    { "a=10 b=2 c=3 Testing one two",   "a", 10, NULL, "Testing one two", 0, 0},
    { "a=10 b=2 c=3 Meep = Moop",       "b", 2, NULL, "Meep = Moop", 0, 0},
    { "a=10 b=2 c=-3",                  "c", -3, NULL, NULL, 0, 0},
    { "a=b=c=3",                        "a", 0, NULL, NULL, -1, EINVAL},
    { "foo=x42",                        "foo", 0, NULL, NULL, -1, EINVAL },
    { "foo=42x",                        "foo", 0, NULL, NULL, -1, EINVAL },
    { "foo=bar",                        "foo", 0, NULL, NULL, -1, EINVAL },
    { "foo= 1",                         "foo", 0, NULL, "1", -1, EINVAL },
    { "foo=",                           "foo", 0, NULL, NULL, -1, EINVAL },
    { "type=cancel severity=7 userid=42", "severity", 7, NULL, NULL, 0, 0 },
    { "type=cancel severity=7 userid=42 Hah!", "userid", 42, NULL, "Hah!", 0, 0 },
    { "",                               "foo", 42, NULL, NULL, -1, ENOENT },

    /* String */
    { "type=cancel severity=7 userid=42", "type", 0, "cancel", NULL, 0, 0 },
    { "foo=42",                         "foo", 0, "42", NULL, 0, 0 },
    { "a=foo b= c=bar One!",            "a", 0, "foo", "One!", 0, 0},
    { "a=foo b= c=bar Two!",            "b", 0, "", "Two!", 0, 0},
    { "a=foo b= c=bar Three!",          "c", 0, "bar", "Three!", 0, 0},
    { "",                               "foo", 0, "bar", NULL, -1, ENOENT },

    /* End */
    { NULL, NULL, 0, NULL, NULL, 0, 0 },
};

void check_one_context (struct context_input *c)
{
    int rc;
    const char *s;

    if (c->strval) {
        char val[64] = "";
        errno = 0;
        rc = util_str_from_context (c->context, c->key, val, sizeof (val));
        ok (rc == c->rc && (rc != 0 || !strcmp (val, c->strval))
                        && (rc == 0 || errno == c->errnum),
            "util_str_from_context ctx=%s %s", c->context,
            c->rc == 0 ? "works" : "fails");
    }
    else {
        int val = 0;
        errno = 0;
        rc = util_int_from_context (c->context, c->key, &val);
        ok (rc == c->rc && (rc != 0 || val == c->intval)
                        && (rc == 0 || errno == c->errnum),
            "util_int_from_context ctx=%s %s", c->context,
            c->rc == 0 ? "works" : "fails");
    }
    s = util_note_from_context (c->context);
    ok ((c->note == NULL && s == NULL)
        || (c->note && s && !strcmp (s, c->note)),
        "util_note_from_context ctx=%s returned %s", c->context,
        c->note ? c->note : "NULL");
}

void check_context (void)
{
    int i;
    for (i = 0; contexttab[i].context != NULL; i++)
        check_one_context (&contexttab[i]);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    check_jobkey ();
    check_context ();

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
