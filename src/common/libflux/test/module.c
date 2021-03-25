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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

/* N.B. FAKE1 and FAKE2 are defined with -D on the CC command line.
 * They are set to the full path of two test modules, module_fake1.so
 * and module_fake2.so.  module_fake1.so simply defines mod_name to "fake1".
 * module_fake2.so omits the mod_name symbol to cause an error.
 */

int errmsg_count;
void errmsg_cb (const char *msg, void *arg)
{
    diag ("%s", msg);
    errmsg_count++;
}

void test_modname (void)
{
    char *name;

    name = flux_modname (FAKE1, NULL, NULL);
    ok (name != NULL && !strcmp (name, "fake1"),
        "flux_modname path=module_fake1 works");
    free (name);

    errno = 0;
    errmsg_count = 0;
    name = flux_modname (FAKE2, errmsg_cb, NULL);
    ok (name == NULL && errno == EINVAL && errmsg_count == 1,
        "flux_modname path=module_fake2 fails with EINVAL and extended error");

    errmsg_count = 0;
    name = flux_modname (FAKE2, NULL, NULL);
    ok (name == NULL && errmsg_count == 0,
        "flux_modname moderr callback can be NULL");

    errno = 0;
    errmsg_count = 0;
    name = flux_modname ("/noexist", errmsg_cb, NULL);
    ok (name == NULL && errno == ENOENT && errmsg_count == 1,
        "flux_modname path=/noexist fails with ENOENT and extended error");

    errno = 0;
    errmsg_count = 0;
    name = flux_modname (NULL, errmsg_cb, NULL);
    ok (name == NULL && errno == EINVAL && errmsg_count == 0,
        "flux_modname path=NULL fails with EINVAL and no extended error");
}

/* modfind test:
 * Create 3 directory 'searchpath' containing symlinks to test modules.
 * module fake1.so is named 'fake1'.
 * module fake2.so does not define the mod_name symbol.
 */
char dir[3][PATH_MAX + 1];
char searchpath[3*PATH_MAX + 4];
char link1[PATH_MAX + 1];
char link2[PATH_MAX + 1];

void test_modfind_init (void)
{
    const char *tmpdir = getenv ("TMPDIR");
    int i, n;

    if (!tmpdir)
        tmpdir = "/tmp";
    for (i = 0; i < 3; i++) {
        n = snprintf (dir[i], sizeof (dir[i]), "%s/modfind.XXXXXX", tmpdir);
        if (n >= sizeof (dir[i]))
            BAIL_OUT ("snprintf buffer overflow");
        if (!mkdtemp (dir[i]))
            BAIL_OUT ("mkdtemp: %s", strerror (errno));
        if (strlen (searchpath) > 0)
            strcat (searchpath, ":");
        strcat (searchpath, dir[i]);
    }
    /* Symlink test modules into dirs 1 and 2 */
    n = snprintf (link1, sizeof (link1), "%s/fake1.so", dir[1]);
    if (n >= sizeof (dir[i]))
        BAIL_OUT ("snprintf buffer overflow");
    n = snprintf (link2, sizeof (link2), "%s/fake2.so", dir[2]);
    if (n >= sizeof (dir[i]))
        BAIL_OUT ("snprintf buffer overflow");
    if (symlink (FAKE1, link1) < 0)
        BAIL_OUT ("symlink %s: %s", link1, strerror (errno));
    if (symlink (FAKE2, link2) < 0)
        BAIL_OUT ("symlink %s: %s", link2, strerror (errno));
}

void test_modfind_fini (void)
{
    int i;

    if (unlink (link1) < 0)
        BAIL_OUT ("unlink %s: %s", link1, strerror (errno));
    if (unlink (link2) < 0)
        BAIL_OUT ("unlink %s: %s", link2, strerror (errno));
    for (i = 0; i < 3; i++) {
        if (rmdir (dir[i]) < 0)
            BAIL_OUT ("unlink %s: %s", dir[i], strerror (errno));
    }
}

void test_modfind (void)
{
    char *path;

    test_modfind_init ();

    path = flux_modfind (searchpath, "fake1", errmsg_cb, NULL);
    ok (path != NULL && !strcmp (path, link1),
        "flux_modfind modname=fake1 returns correct path");
    free (path);

    errno = 0;
    errmsg_count = 0;
    path = flux_modfind (searchpath, "fake2", errmsg_cb, NULL);
    ok (path == NULL && errno == ENOENT && errmsg_count == 1,
        "flux_modfind modname=fake2 fails with ENOENT and extended error");

    errno = 0;
    path = flux_modfind (searchpath, NULL, NULL, NULL);
    ok (path == NULL && errno == EINVAL,
        "flux_modfind modname=NULL fails with EINVAL");

    errno = 0;
    path = flux_modfind (NULL, "fake1", NULL, NULL);
    ok (path == NULL && errno == EINVAL,
        "flux_modfind searchpath=NULL fails with EINVAL");

    test_modfind_fini ();
}

void test_debug (void)
{
    flux_t *h;
    struct flux_handle_ops ops;
    int flags;

    /* Create dummy handle with no capability - only aux hash */
    memset (&ops, 0, sizeof (ops));
    if (!(h = flux_handle_create (NULL, &ops, 0)))
        BAIL_OUT ("flux_handle_create failed");

    ok (flux_module_debug_test (h, 1, false) == false,
        "flux_module_debug_test returns false with unpopulated aux");

    if (flux_aux_set (h, "flux::debug_flags", &flags, NULL) < 0)
        BAIL_OUT ("flux_aux_set failed");

    flags = 0x0f;
    ok (flux_module_debug_test (h, 0x10, false) == false,
        "flux_module_debug_test returns false on false flag (clear=false)");
    ok (flux_module_debug_test (h, 0x01, false) == true,
        "flux_module_debug_test returns true on true flag (clear=false)");
    ok (flags == 0x0f,
        "flags are unaltered after testing with clear=false");

    ok (flux_module_debug_test (h, 0x01, true) == true,
        "flux_module_debug_test returns true on true flag (clear=true)");
    ok (flags == 0x0e,
        "flag was cleared after testing with clear=true");

    flux_handle_destroy (h);
}

void test_set_running (void)
{
    flux_t *h;

    if (!(h = loopback_create (0)))
        BAIL_OUT ("loopback_create failed");

    ok (flux_module_set_running (h) == 0,
        "flux_module_set_running returns success");
    errno = 0;
    ok (flux_module_set_running (NULL) < 0 && errno == EINVAL,
        "flux_module_set_running h=NULL fails with EINVAL");

    flux_close (h);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_modname ();
    test_modfind ();
    test_debug ();
    test_set_running ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

