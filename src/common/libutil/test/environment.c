/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/environment.h"

static void test_var_next ()
{
    const char *entry = NULL;
    struct environment *e = environment_create ();
    if (e == NULL)
        BAIL_OUT ("Failed to create environment object!");
    ok (environment_var_next (e, "PATH", NULL) == NULL,
        "environment_var_next () returns NULL for missing env var");
    environment_set (e, "PATH", "/bin:/usr/bin:/usr/local/bin", ':');
    diag ("set PATH=/bin:/usr/bin:/usr/local/bin");
    ok ((entry = environment_var_next (e, "PATH", entry)) != NULL,
        "environment_var_next () works");
    is (entry, "/bin",
        "environment_var_next returns first element");
    ok ((entry = environment_var_next (e, "PATH", entry)) != NULL,
        "environment_var_next () works");
    is (entry, "/usr/bin",
        "environment_var_next returns next element");
    ok ((entry = environment_var_next (e, "PATH", entry)) != NULL,
        "environment_var_next () works");
    is (entry, "/usr/local/bin",
        "environment_var_next returns last element");
    ok (!(entry = environment_var_next (e, "PATH", entry)),
        "environment_var_next () returns NULL after last element");
    environment_destroy (e);
}

static void test_insert ()
{
    const char *entry = NULL;
    struct environment *e = environment_create ();
    if (e == NULL)
        BAIL_OUT ("Failed to create environment object!");
    ok (environment_insert (e, "PATH", "/bin", "/foo") < 0 && errno == ENOENT,
        "environment_insert on missing key returns ENOENT");
    environment_set (e, "PATH", "/bin:/usr/bin:/usr/local/bin", ':');
    diag ("set PATH=/bin:/usr/bin:/usr/local/bin");
    diag ("searching for entry=/usr/bin");
    while ((entry = environment_var_next (e, "PATH", entry)))
        if (streq (entry, "/usr/bin"))
            break;
    diag ("entry=%s", entry);
    ok (environment_insert (e, "PATH", (char *) entry, "/new/path") == 0,
        "environment_insert /new/path before /usr/bin return success");
    is (environment_get (e, "PATH"),
        "/bin:/new/path:/usr/bin:/usr/local/bin",
        "PATH is now /bin:/new/path:/usr/bin:/usr/local/bin");
    environment_destroy (e);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_var_next ();
    test_insert ();

    done_testing ();

    return 0;
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
