/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <stdio.h>

#include "attr.h"

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

void basic (void)
{
    attr_t *attrs;
    const char *val;
    int flags;

    ok ((attrs = attr_create ()) != NULL,
        "attr_create works");

    /* attr_get, attr_set on unknown fails
     */
    errno = 0;
    ok (attr_get (attrs, "foo", NULL, NULL) < 0 && errno == ENOENT,
        "attr_get on unknown attr fails with errno == ENOENT");
    errno = 0;
    ok (attr_set (attrs, "foo", "bar") < 0 && errno == ENOENT,
        "attr_set on unknown attr fails with errno == ENOENT");

    /* attr_add, attr_get works
     */
    ok ((attr_add (attrs, "foo", "bar", 0) == 0),
        "attr_add works");
    errno = 0;
    ok ((attr_add (attrs, "foo", "bar", 0) < 0 && errno == EEXIST),
        "attr_add on existing attr fails with EEXIST");
    ok (attr_get (attrs, "foo", NULL, NULL) == 0,
        "attr_get on new attr works with NULL args");
    val = NULL;
    flags = 0;
    ok (attr_get (attrs, "foo", &val, &flags) == 0 && streq (val, "bar")
        && flags == 0,
        "attr_get on new attr works returns correct val,flags");

    /* attr_delete works
     */
    ok (attr_delete (attrs, "foo", false) == 0,
        "attr_delete works");
    errno = 0;
    ok (attr_get (attrs, "foo", NULL, NULL) < 0 && errno == ENOENT,
        "attr_get on deleted attr fails with errno == ENOENT");

    /* ATTR_IMMUTABLE protects against update/delete from user;
     * update/delete can NOT be forced on broker.
     */
    ok (attr_add (attrs, "foo", "baz", ATTR_IMMUTABLE) == 0,
        "attr_add ATTR_IMMUTABLE works");
    flags = 0;
    val = NULL;
    ok (attr_get (attrs, "foo", &val, &flags) == 0 && streq (val, "baz")
        && flags == ATTR_IMMUTABLE,
        "attr_get returns correct value and flags");
    errno = 0;
    ok (attr_set (attrs, "foo", "bar") < 0 && errno == EPERM,
        "attr_set on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_set (attrs, "foo", "baz")  < 0 && errno == EPERM,
        "attr_set (force) on immutable fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "foo", false) < 0 && errno == EPERM,
        "attr_delete on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "foo", true) < 0 && errno == EPERM,
        "attr_delete (force) on immutable fails with EPERM");

    /* Add couple more attributes and exercise iterator.
     * initial hash contents: foo=bar
     */
    val = attr_first (attrs);
    ok (val && streq (val, "foo"),
        "attr_first returned foo");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");
    ok (attr_add (attrs, "foo1", "42", 0) == 0
        && attr_add (attrs, "foo2", "43", 0) == 0
        && attr_add (attrs, "foo3", "44", 0) == 0
        && attr_add (attrs, "foo4", "44", 0) == 0,
        "attr_add foo1, foo2, foo3, foo4 works");
    val = attr_first (attrs);
    ok (val && strstarts (val, "foo"),
        "attr_first returned foo-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "foo"),
        "attr_next returned foo-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "foo"),
        "attr_next returned foo-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "foo"),
        "attr_next returned foo-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "foo"),
        "attr_next returned foo-prefixed attr");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");

    attr_destroy (attrs);
}

void active (void)
{
    attr_t *attrs;
    const char *val;
    int a, c;
    uint32_t b;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    /* attr_add_active (int helper)
     */
    ok (attr_add_active_int (attrs, "a", &a, 0) == 0,
        "attr_add_active_int works");
    a = 0;
    ok (attr_get (attrs, "a", &val, NULL) == 0 && val && streq (val, "0"),
        "attr_get on active int tracks val=0");
    a = 1;
    ok (attr_get (attrs, "a", &val, NULL) == 0 && streq (val, "1"),
        "attr_get on active int tracks val=1");
    a = -1;
    ok (attr_get (attrs, "a", &val, NULL) == 0 && streq (val, "-1"),
        "attr_get on active int tracks val=-1");
    a = INT_MAX - 1;
    ok (attr_get (attrs, "a", &val, NULL) == 0
        && strtol (val, NULL, 10) == INT_MAX - 1,
        "attr_get on active int tracks val=INT_MAX-1");
    a = INT_MIN + 1;
    ok (attr_get (attrs, "a", &val, NULL) == 0
        && strtol (val, NULL, 10) == INT_MIN + 1,
        "attr_get on active int tracks val=INT_MIN+1");

    ok (attr_set (attrs, "a", "0") == 0 && a == 0,
        "attr_set on active int sets val=0");
    ok (attr_set (attrs, "a", "1") == 0 && a == 1,
        "attr_set on active int sets val=1");
    ok (attr_set (attrs, "a", "-1") == 0 && a == -1,
        "attr_set on active int sets val=-1");
    ok (attr_delete (attrs, "a", true) == 0,
        "attr_delete (force) works on active attr");

    /* attr_add_active (uint32_t helper)
     */
    ok (attr_add_active_uint32 (attrs, "b", &b, 0) == 0,
        "attr_add_active_uint32 works");
    b = 0;
    ok (attr_get (attrs, "b", &val, NULL) == 0 && val && streq (val, "0"),
        "attr_get on active uin32_t tracks val=0");
    b = 1;
    ok (attr_get (attrs, "b", &val, NULL) == 0 && streq (val, "1"),
        "attr_get on active uint32_t tracks val=1");
    b = UINT_MAX - 1;
    ok (attr_get (attrs, "b", &val, NULL) == 0
        && strtoul (val, NULL, 10) == UINT_MAX - 1,
        "attr_get on active uint32_t tracks val=UINT_MAX-1");

    ok (attr_set (attrs, "b", "0") == 0 && b == 0,
        "attr_set on active uint32_t sets val=0");
    ok (attr_set (attrs, "b", "1") == 0 && b == 1,
        "attr_set on active uint32_t sets val=1");
    ok (attr_delete (attrs, "b", true) == 0,
        "attr_delete (force) works on active attr");

    /* immutable active int works as expected
     */
    ok (attr_add_active_int (attrs, "c", &c, ATTR_IMMUTABLE) == 0,
        "attr_add_active_int ATTR_IMMUTABLE works");
    c = 42;
    ok (attr_get (attrs, "c", &val, NULL) == 0 && val && streq (val, "42"),
        "attr_get returns initial val=42");
    c = 43;
    ok (attr_get (attrs, "c", &val, NULL) == 0 && val && streq (val, "42"),
        "attr_get ignores value changes");
    errno = 0;
    ok (attr_delete (attrs, "c", true) < 0 && errno == EPERM,
        "attr_delete (force) fails with EPERM");

    attr_destroy (attrs);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    basic ();
    active ();

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
