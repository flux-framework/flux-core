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
#include "src/common/libutil/errprintf.h"
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
    ok (attr_get (attrs, "test.foo", NULL, NULL) < 0 && errno == ENOENT,
        "attr_get on unknown attr fails with errno == ENOENT");
    errno = 0;
    ok (attr_set (attrs, "test.foo", "bar") < 0 && errno == ENOENT,
        "attr_set on unknown attr fails with errno == ENOENT");

    /* attr_add, attr_get works
     */
    ok ((attr_add (attrs, "test.foo", "bar", 0) == 0),
        "attr_add works");
    errno = 0;
    ok ((attr_add (attrs, "test.foo", "bar", 0) < 0 && errno == EEXIST),
        "attr_add on existing attr fails with EEXIST");
    ok (attr_get (attrs, "test.foo", NULL, NULL) == 0,
        "attr_get on new attr works with NULL args");
    val = NULL;
    flags = 0;
    ok (attr_get (attrs, "test.foo", &val, &flags) == 0 && streq (val, "bar")
        && flags == 0,
        "attr_get on new attr works returns correct val,flags");

    /* attr_delete works
     */
    ok (attr_delete (attrs, "test.foo") == 0,
        "attr_delete works");
    errno = 0;
    ok (attr_get (attrs, "test.foo", NULL, NULL) < 0 && errno == ENOENT,
        "attr_get on deleted attr fails with errno == ENOENT");

    /* ATTR_IMMUTABLE protects against update/delete from user;
     * update/delete can NOT be forced on broker.
     */
    ok (attr_add (attrs, "test.foo", "baz", ATTR_IMMUTABLE) == 0,
        "attr_add ATTR_IMMUTABLE works");
    flags = 0;
    val = NULL;
    ok (attr_get (attrs, "test.foo", &val, &flags) == 0 && streq (val, "baz")
        && flags == ATTR_IMMUTABLE,
        "attr_get returns correct value and flags");
    errno = 0;
    ok (attr_set (attrs, "test.foo", "bar") < 0 && errno == EPERM,
        "attr_set on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_set (attrs, "test.foo", "baz")  < 0 && errno == EPERM,
        "attr_set (force) on immutable fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "test.foo") < 0 && errno == EPERM,
        "attr_delete on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "test.foo") < 0 && errno == EPERM,
        "attr_delete on immutable fails with EPERM");

    /* Add couple more attributes and exercise iterator.
     * initial hash contents: foo=bar
     */
    val = attr_first (attrs);
    ok (val && streq (val, "test.foo"),
        "attr_first returned test.foo");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");
    ok (attr_add (attrs, "test.foo1", "42", 0) == 0
        && attr_add (attrs, "test.foo2", "43", 0) == 0
        && attr_add (attrs, "test.foo3", "44", 0) == 0
        && attr_add (attrs, "test.foo4", "44", 0) == 0,
        "attr_add test.foo[1-4] works");
    val = attr_first (attrs);
    ok (val && strstarts (val, "test"),
        "attr_first returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");

    attr_destroy (attrs);
}

int active_get (const char *name, const char **val, void *arg)
{
    const char **cpp = arg;
    *val = *cpp;
    return 0;
}

int active_set (const char *name, const char *val, void *arg)
{
    const char **cpp = arg;
    *cpp = val;
    return 0;
}


void active (void)
{
    attr_t *attrs;
    const char *val;
    const char *a;
    const char *b;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    ok (attr_add_active (attrs,
                         "test.a",
                         0,
                         active_get,
                         active_set,
                         &a) == 0,
        "attr_add_active works");
    a = "x";
    ok (attr_get (attrs, "test.a", &val, NULL) == 0 && val && streq (val, "x"),
        "attr_get on active a tracks val=x");
    a = "y";
    ok (attr_get (attrs, "test.a", &val, NULL) == 0 && streq (val, "y"),
        "attr_get on active a tracks val=y");
    a = NULL;
    ok (attr_get (attrs, "test.a", &val, NULL) == 0 && val == NULL,
        "attr_get on active a tracks val=NULL");
    ok (attr_delete (attrs, "test.a") == 0,
        "attr_delete works on active attr");

    /* immutable active works as expected
     */
    ok (attr_add_active (attrs,
                         "test.b",
                         ATTR_IMMUTABLE,
                         active_get,
                         active_set,
                         &b) == 0,
        "attr_add_active ATTR_IMMUTABLE works");
    b = "m";
    ok (attr_get (attrs, "test.b", &val, NULL) == 0 && val && streq (val, "m"),
        "attr_get returns initial val=m");
    b = "n";
    ok (attr_get (attrs, "test.b", &val, NULL) == 0 && val && streq (val, "m"),
        "attr_get ignores value changes");
    errno = 0;
    ok (attr_delete (attrs, "test.b") < 0 && errno == EPERM,
        "attr_delete fails with EPERM");

    attr_destroy (attrs);
}

void unknown (void)
{
    attr_t *attrs;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    errno = 0;
    ok (attr_add (attrs, "unknown", "foo", 0) < 0 && errno == ENOENT,
        "attr_add of unknown attribute fails with ENOENT");

    errno = 0;
    ok (attr_add_active (attrs, "unknown", 0, NULL, NULL, NULL) < 0
        && errno == ENOENT,
        "attr_add_active of unknown attribute fails with ENOENT");

    attr_destroy (attrs);
}

void cmdline (void)
{
    attr_t *attrs;
    flux_error_t error;
    int rc;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    ok (attr_set_cmdline (attrs, "test.foo", "bar", &error) == 0,
        "attr_set_cmdline test.foo works");

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, "unknown", "foo", &error);
    ok (rc < 0 && errno == ENOENT,
        "attr_set_cmdline attr=unknown fails with ENOENT");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, "test-ro.foo", "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attr=test-ro.foo fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, NULL, "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attr=NULL fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (NULL, "test.foo", "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attrs=NULL fails with EINVAL");
    diag ("%s", error.text);

    attr_destroy (attrs);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    basic ();
    active ();
    unknown ();
    cmdline ();

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
