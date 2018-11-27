#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/common/libflux/module.h"
#include "src/common/libtap/tap.h"

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
    const char *path;

    test_modfind_init ();

    path = flux_modfind (searchpath, "fake1", errmsg_cb, NULL);
    ok (path != NULL && !strcmp (path, link1),
        "flux_modfind modname=fake1 returns correct path");

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

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_modname ();
    test_modfind ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

