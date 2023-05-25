/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"
#include "src/common/libutil/grudgeset.h"

int main (int argc, char *argv[])
{
    struct grudgeset *gs = NULL;

    plan (NO_PLAN);

    ok (grudgeset_add (NULL, NULL) < 0 && errno == EINVAL,
        "grudgeset_add (NULL, NULL) returns EINVAL");
    ok (grudgeset_add (&gs, NULL) < 0 && errno == EINVAL,
        "grudgeset_add (&o, NULL) returns EINVAL");
    ok (grudgeset_remove (NULL, NULL) < 0 && errno == EINVAL,
        "grudgeset_remove (NULL, NULL) returns EINVAL");

    ok (grudgeset_remove (NULL, "foo") < 0 && errno == ENOENT,
        "grudgeset_remove(NULL, \"foo\") returns ENOENT");
    ok (grudgeset_size (NULL) == 0,
        "grudgeset_size (NULL) == 0");

    ok (grudgeset_used (NULL, "foo") == 0,
        "grudgeset_used (NULL, \"foo\") returns 0");
    ok (grudgeset_contains (NULL, NULL) == 0,
        "grudgeset_contains (NULL, NULL) returns 0");
    ok (grudgeset_contains (NULL, "foo") == 0,
        "grudgeset_contains (NULL, \"foo\") returns 0");
    ok (grudgeset_tojson (NULL) == NULL,
        "grudgeset_tojson (NULL) returns NULL");

    ok (grudgeset_add (&gs, "foo") == 0,
        "grudgeset_add works with NULL object");
    ok (gs != NULL,
        "grudgeset is now non-NULL");
    ok (grudgeset_size (gs) == 1,
        "set is of size 1");
    ok (grudgeset_contains (gs, "foo") == 1,
        "grudgeset_contains (foo) works");

    ok (grudgeset_add (&gs, "foo") < 0 && errno == EEXIST,
        "grudgeset_add of existing value returns EEXIST");

    ok (grudgeset_add (&gs, "baz") == 0,
        "grudgeset_add of a second value works");
    ok (grudgeset_size (gs) == 2,
        "grudgeset is of size 2");
    ok (grudgeset_contains (gs, "baz") == 1,
        "grudgeset_contains (baz) works");

    ok (grudgeset_tojson (gs) != NULL,
        "grudgeset_tojson() is non-NULL");

    ok (grudgeset_remove (gs, "xxyyzz") < 0 && errno == ENOENT,
        "grudgeset_remove of nonexistent entry returns ENOENT");

    ok (grudgeset_remove (gs, "foo") == 0,
        "grudgeset_remove of first item in set works");
    ok (grudgeset_size (gs) == 1,
        "set is of size 1");
    ok (grudgeset_contains (gs, "foo") == 0,
        "grudgeset no longer contains foo");
    ok (grudgeset_used (gs, "foo") == 1,
        "but grudgeset has marked foo as \"used\"");

    ok (grudgeset_add (&gs, "foo") < 0 && errno == EEXIST,
        "grudgeset_add of removed value still fails");

    ok (grudgeset_remove (gs, "baz") == 0,
        "grudgeset_remove of second element works");
    ok (grudgeset_size (gs) == 0,
        "grudgeset_size is now 0");
    ok (grudgeset_used (gs, "baz"),
        "grudgeset_used (baz) == 1");

    grudgeset_destroy (gs);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
