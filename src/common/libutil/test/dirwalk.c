/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define _GNU_SOURCE
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>

#include <dirent.h>

#include <czmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/dirwalk.h"

static int makepath (const char *fmt, ...)
{
    int rc;
    int errnum = 0;
    char *pp;
    char *sp;
    char *path = NULL;
    va_list ap;

    va_start (ap, fmt);
    rc = vasprintf (&path, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return -1;

    // If mkdir works on path, we can skip attempted mkdir of all parents
    if (mkdir (path, 0700) == 0)
        goto out;

    pp = path;
    while ((sp = strchr (pp, '/'))) {
        if (sp > pp) {
            *sp = '\0';
            if ((mkdir (path, 0777) < 0) && (errno != EEXIST)) {
                errnum = errno;
                goto out;
            }
            *sp = '/';
        }
        pp = sp + 1;
    }
    if (errnum == 0 && (mkdir (path, 0777) < 0))
        errnum = errno;
out:
    free (path);
    errno = errnum;
    return errnum == 0 ? 0 : -1;
}

static int vcreat (const char *fmt, ...)
{
    int rc;
    char *path = NULL;
    va_list ap;
    va_start (ap, fmt);
    rc = vasprintf (&path, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return (-1);

    if ((rc = creat (path, 0700)) >= 0)
        close (rc);

    free (path);
    return (rc);
}

static char *create_test_dir ()
{
    const char *tmpdir = getenv ("TMPDIR");
    const char *tmp = tmpdir ? tmpdir : "/tmp";
    char path[PATH_MAX];
    int n;

    n = snprintf (path, sizeof (path), "%s/dirwalk_test.XXXXXX", tmp);
    if ((n <= 0) || (n >= sizeof (path)))
        BAIL_OUT ("Unable to create temporary directory string");

    if (!mkdtemp (path))
        BAIL_OUT ("mkdtemp failure");

    return strdup (path);
}

static int find_dir (dirwalk_t *d, void *arg)
{
    return (dirwalk_isdir (d) ? 1 : 0);
}

static int return_err (dirwalk_t *d, void *arg)
{
    if (!dirwalk_isdir (d))
        dirwalk_stop (d, 42);
    return 0;
}

static int check_stat (dirwalk_t *d, void *arg)
{
    const struct stat *sb = dirwalk_stat (d);
    if (sb == NULL) {
        dirwalk_stop (d, 1);
        diag ("dirwalk_stat for %s failed\n", dirwalk_path (d));
    } else if (dirwalk_isdir (d)) {
        if (!S_ISDIR (sb->st_mode)) {
            diag ("dirwalk_isdir() but sb->st_mode = 0x%08x\n", sb->st_mode);
            dirwalk_stop (d, 1);
        }
    } else if (sb->st_size < 0) {
        diag ("sb->st_size = %ju", (uintmax_t)sb->st_size);
        dirwalk_stop (d, 1);
    }
    return 0;
}

static int check_dirfd (dirwalk_t *d, void *arg)
{
    struct stat st;
    const struct stat *sb = dirwalk_stat (d);
    int dirfd = dirwalk_dirfd (d);
    /*  Compare stat by fstatat() with internal dirwalk stat() to ensure
     *   dirfd points to this entry's parent directory.
     */
    if (fstatat (dirfd, dirwalk_name (d), &st, 0) < 0) {
        dirwalk_stop (d, errno);
    } else if (sb->st_dev != st.st_dev || sb->st_ino != st.st_ino) {
        diag ("check_dirfd: st_dev or st_ino do not match");
        dirwalk_stop (d, 1);
    }
    return 0;
}

int check_zlist_order (zlist_t *l, const char *base, char *expected[])
{
    int i = 0;
    char *dir;
    dir = zlist_first (l);
    while (dir) {
        diag ("zlist_order: %d: %s", i, dir);
        int result;
        char *exp;
        if (expected[i] == NULL) {
            diag ("check_zlist: more results than expected=%d\n", i - 1);
            return 0;
        }
        if (asprintf (&exp, "%s%s", base, expected[i]) < 0)
            BAIL_OUT ("asprintf");

        result = strcmp (exp, dir);
        if (result != 0) {
            diag ("check_zlist: %d: expected %s got %s", i, exp, dir);
            free (exp);
            return 0;
        }
        free (exp);
        i++;
        dir = zlist_next (l);
    }
    return 1;
}

static int d_unlinkat (dirwalk_t *d, void *arg)
{
    int rc = unlinkat (dirwalk_dirfd (d),
                       dirwalk_name (d),
                       dirwalk_isdir (d) ? AT_REMOVEDIR : 0);
    if (rc < 0)
        dirwalk_stop (d, errno);
    return 0;
}

static int make_a_link (const char *targetbase,
                        const char *target,
                        const char *linkbase,
                        const char *linkname)
{
    int rc = -1;
    char *l = NULL, *t = NULL;
    if ((asprintf (&l, "%s/%s", linkbase, linkname) >= 0)
        && (asprintf (&t, "%s/%s", targetbase, target) >= 0))
        rc = symlink (t, l);
    free (l);
    free (t);
    return rc;
}

int main (int argc, char **argv)
{
    char *s, *rpath, *tmp = NULL, *tmp2 = NULL;
    int n;

    plan (NO_PLAN);

    if (!(tmp = create_test_dir ()) || !(tmp2 = create_test_dir ()))
        BAIL_OUT ("unable to create test directory");

    n = dirwalk (tmp, 0, NULL, NULL);
    ok (n == 1, "dirwalk of empty directory visits one directory");
    n = dirwalk (tmp, DIRWALK_DEPTH, NULL, NULL);
    ok (n == 1, "dirwalk of empty directory with DIRWALK_DEPTH works");

    if (makepath ("%s/a", tmp) < 0)
        BAIL_OUT ("makepath failed");

    n = dirwalk (tmp, 0, NULL, NULL);
    ok (n == 2, "dirwalk of directory with 1 entry returns 2");
    n = dirwalk (tmp, DIRWALK_DEPTH, NULL, NULL);
    ok (n == 2, "dirwalk of directory with 1 entry DIRWALK_DEPTH returns 2");

    if (makepath ("%s/a/b/c", tmp) < 0)
        BAIL_OUT ("makepath failed");

    n = dirwalk (tmp, 0, NULL, NULL);
    ok (n == 4, "dirwalk of deeper dirtree");

    if (makepath ("%s/a/b/c/d", tmp) < 0)
        BAIL_OUT ("makepath failed");

    if (vcreat ("%s/a/foo", tmp) < 0 || vcreat ("%s/a/b/c/foo", tmp) < 0)
        BAIL_OUT ("vcreat");

    if (makepath ("%s/bar", tmp2) < 0)
        BAIL_OUT ("makepath failed");

    if (vcreat ("%s/bar/foo", tmp2) < 0)
        BAIL_OUT ("vcreat");

    if (make_a_link (tmp, "a/b", tmp2, "link") < 0)
        BAIL_OUT ("make_a_link");

    /*  dirwalk_find tests:
     */

    /* Not a directory returns an error */
    zlist_t *l = dirwalk_find ("/etc/passwd", 0, "*", 1, NULL, 0);
    ok (l == NULL && errno == ENOTDIR, "dirwalk_find on file returns ENOTDIR");

    l = dirwalk_find ("/blah:/bloop", 0, "*", 1, NULL, 0);
    ok (l && zlist_size (l) == 0, "dirwalk_find on nonexistent dirs works");
    zlist_destroy (&l);

    /* Find first file matching "foo" */
    l = dirwalk_find (tmp, 0, "foo", 1, NULL, 0);
    ok (l != NULL, "dirwalk_find");
    ok (l && zlist_size (l) == 1, "dirwalk_find stopped at 1 result");
    ok (strcmp (basename (zlist_first (l)), "foo") == 0,
        "breadth-first search got expected match");
    zlist_destroy (&l);

    /* Find all files matching "foo" */
    l = dirwalk_find (tmp, 0, "foo", 0, NULL, 0);
    ok (l != NULL, "dirwalk with find callback");
    ok (l && zlist_size (l) == 2, "breadth-first find found all matches");
    zlist_destroy (&l);

    /* Find all files matching "foo" with search path */
    if (asprintf (&s, "%s:%s", tmp, tmp2) < 0)
        BAIL_OUT ("asprintf");
    l = dirwalk_find (s, 0, "foo", 0, NULL, 0);
    ok (l != NULL, "dirwalk_find with search path");
    ok (l && zlist_size (l) == 3, "find with search path found all matches");
    zlist_destroy (&l);
    free (s);

    /* depth-first find */
    l = dirwalk_find (tmp, DIRWALK_DEPTH, "foo", 0, NULL, 0);
    ok (l != NULL, "dirwalk with find callback");
    ok (l && zlist_size (l) == 2, "depth-first find found all results");
    zlist_destroy (&l);

    /* Special directory walk tests.
     */
    int flags = DIRWALK_DEPTH | DIRWALK_FIND_DIR;
    l = dirwalk_find (tmp, flags, "*", 0, find_dir, NULL);
    ok (l && zlist_size (l) > 0, "dirwalk to find all dirs works");

    char *expect_depth[] = {"/a/b/c/d", "/a/b/c", "/a/b", "/a", ""};
    ok (l && check_zlist_order (l, tmp, expect_depth),
        "depth-first visited directories in correct order");
    zlist_destroy (&l);

    flags = DIRWALK_FIND_DIR;
    l = dirwalk_find (tmp, flags, "*", 0, find_dir, NULL);
    ok (l && zlist_size (l) > 0, "dirwalk to find all dirs works");

    char *expect_breadth[] = {
        "",
        "/a",
        "/a/b",
        "/a/b/c",
        "/a/b/c/d",
    };
    ok (l && check_zlist_order (l, tmp, expect_breadth),
        "breadth-first visited directories in correct order");
    zlist_destroy (&l);

    char *cwd = get_current_dir_name ();
    if (!cwd || (chdir (tmp) < 0))
        BAIL_OUT ("chdir %s", tmp);

    flags |= DIRWALK_REALPATH;
    l = dirwalk_find (tmp, flags, "*", 0, find_dir, NULL);
    ok (l && zlist_size (l) > 0, "dirwalk works with DIRWALK_REALPATH");

    /* tmp base for comparison must also be realpath-ed */
    rpath = realpath (tmp, NULL);
    ok (l && check_zlist_order (l, rpath, expect_breadth),
        "breadth-first visited directories with DIRWALK_REALPATH works");
    zlist_destroy (&l);
    free (rpath);

    if (chdir (cwd) < 0)
        BAIL_OUT ("chdir (%s)", cwd);
    free (cwd);

    n = dirwalk (tmp, 0, return_err, NULL);
    int errnum = errno;
    ok (n == -1, "Error from callback passed to caller");
    ok (errnum == 42, "Error from dirwalk_stop() passed back as errno");

    n = dirwalk (tmp, 0, check_stat, NULL);
    ok (n > 0, "dirwalk_stat works");

    n = dirwalk (tmp, 0, check_dirfd, NULL);
    ok (n > 0, "dirwalk_dirfd works");

    /* Cleanup */
    n = dirwalk (tmp, DIRWALK_DEPTH, d_unlinkat, NULL);
    ok (n == 7, "dirwalk recursive unlink works");

    /* Cleanup */
    n = dirwalk (tmp2, DIRWALK_DEPTH, d_unlinkat, NULL);
    ok (n == 4, "dirwalk recursive unlink works");

    ok (access (tmp, F_OK) < 0 && errno == ENOENT, "tmp working dir removed");
    ok (access (tmp2, F_OK) < 0 && errno == ENOENT, "tmp2 working dir removed");

    free (tmp);
    free (tmp2);
    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
