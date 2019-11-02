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
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glob.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libflux/conf_private.h"

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

const char *t1 = \
"i = 1\n" \
"d = 3.14\n" \
"s = \"foo\"\n" \
"b = true\n" \
"ts = 1979-05-27T07:32:00Z\n" \
"ai = [ 1, 2, 3]\n" \
"[tab]\n" \
"subvalue = 42\n";

const char *tab2 = \
"[tab2]\n" \
"id = 2\n";

const char *tab3 = \
"[tab3]\n" \
"id = 3\n";


static void
create_test_file (const char *dir, char *prefix, char *path, size_t pathlen,
                  const char *contents)
{
    int fd;
    snprintf (path, pathlen, "%s/%s.XXXXXX.toml", dir ? dir : "/tmp", prefix);
    fd = mkstemps (path, 5);
    if (fd < 0)
        BAIL_OUT ("mkstemp %s: %s", path, strerror (errno));
    if (write (fd, contents, strlen (contents)) != strlen (contents))
        BAIL_OUT ("write %s: %s", path, strerror (errno));
    if (close (fd) < 0)
        BAIL_OUT ("close %s: %s", path, strerror (errno));
}

void test_builtin (void)
{
    const char *s1, *s2, *s3;

    s1 = flux_conf_builtin_get ("shell_path", FLUX_CONF_INSTALLED);
    ok (s1 != NULL,
        "flux_conf_builtin_get shell_path INSTALLED works");
    s2 = flux_conf_builtin_get ("shell_path", FLUX_CONF_INTREE);
    ok (s2 != NULL,
        "flux_conf_builtin_get shell_path INTREE works");
    s3 = flux_conf_builtin_get ("shell_path", FLUX_CONF_AUTO);
    ok (s3 != NULL,
        "flux_conf_builtin_get shell_path AUTO works");
    ok (s2 && s3 && !strcmp (s2, s3),
        "AUTO returned INTREE value for test executable");

    errno = 0;
    ok (flux_conf_builtin_get ("notarealkey", FLUX_CONF_INSTALLED) == NULL
        && errno == EINVAL,
        "flux_conf_builtin_get key=notarealkey failed with EINVAL");

    errno = 0;
    ok (flux_conf_builtin_get (NULL, FLUX_CONF_INSTALLED) == NULL
        && errno == EINVAL,
        "flux_conf_builtin_get key=NULL failed with EINVAL");
}

void test_basic (void)
{
    int rc;
    int len;
    const char *tmpdir = getenv ("TMPDIR");
    char dir[PATH_MAX + 1];
    char path1[PATH_MAX + 1];
    char path2[PATH_MAX + 1];
    char path3[PATH_MAX + 1];
    char invalid[PATH_MAX + 1];
    char p[PATH_MAX + 1];
    flux_conf_error_t error;
    flux_conf_t *conf;
    int i, j, k;
    double d;
    const char *s;
    int b;

    /* Create *.toml containing test data
     */
    snprintf (dir, sizeof (dir), "%s/cf.XXXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp %s: %s", dir, strerror (errno));

    create_test_file (dir, "01", path1, sizeof (path1), t1);
    create_test_file (dir, "02", path2, sizeof (path2), tab2);
    create_test_file (dir, "03", path3, sizeof (path3), tab3);

    len = snprintf (p, sizeof (p), "%s/*.toml", dir);
    if ((len < 0) || (len >= sizeof (p)))
        BAIL_OUT ("snprintf failed in creating toml file path");

    /* Parse it
     */
    conf = conf_parse (p, &error);
    ok (conf != NULL,
        "conf_parse successfully parsed 3 files");
    if (!conf)
        BAIL_OUT ("cannot continue without config object");

    /* Check scalar contents
     */
    i = 0;
    d = 0;
    s = NULL;
    b = false;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:i s:f s:s s:b}",
                           "i", &i,
                           "d", &d,
                           "s", &s,
                           "b", &b);
    ok (rc == 0,
        "unpacked config object, scalar values");
    ok (i == 1,
        "unpacked integer value");
    ok (d == 3.14,
        "unpacked double value");
    ok (b == true,
        "unpacked boolean value");
    ok (s != NULL && !strcmp (s, "foo"),
        "unpacked string value");

    /* Check array contents
     */
    i = j = k = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:[i,i,i]}",
                           "ai",
                           &i, &j, &k);
    ok (rc == 0 && i == 1 && j == 2 && k == 3,
        "unpacked array value");

    /* N.B. skip fully decoding timestamp object for now.
     * Not sure if we'll need support for it in this interface.
     * If we do, see tomltk.c for encoding.
     */
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:s}}",
                           "ts",
                           "iso-8601-ts",
                           &s);
    ok (rc == 0,
        "unpacked timestamp value", s);
    diag ("timestamp=%s", s);

    /* Check table contents
     */
    i = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:i}}",
                           "tab",
                           "subvalue", &i);
    ok (rc == 0 && i == 42,
        "unpacked integer from [tab] and got expected value");

    /* Check table from second toml file
     */
    i = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:i}}",
                           "tab2",
                           "id", &i);
    ok (rc == 0 && i == 2,
        "unpacked integer from [tab2] and got expected value");

    /* Check table from third toml file
     */
    i = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:i}}",
                           "tab3",
                           "id", &i);
    ok (rc == 0 && i == 3,
        "unpacked integer from [tab3] and got expected value");

    /* Try to get something that's missing
     */
    errno = 0;
    ok (flux_conf_unpack (conf, &error, "{s:s}", "noexist", &s) < 0
        && errno == EINVAL,
        "flux_conf_unpack key=noexist failed with EINVAL");
    ok (strstr (error.errbuf, "noexist") != NULL,
        "and errbuf mentions noexist");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);

    /* Bad args fail with EINVAL
     */
    errno = 0;
    ok (flux_conf_unpack (NULL, &error, "{s:i}", "i", &i) < 0
        && errno == EINVAL,
        "flux_conf_unpack conf=NULL fails with EINVAL");

    conf_destroy (conf);

    /* Now make an invalid file and ensure cf_update_glob() aborts
     * all updates after any one failure
     */
    create_test_file (dir, "99", invalid, sizeof (invalid), "key = \n");

    conf = conf_parse (p, &error);
    ok (conf == NULL,
        "conf_parse choked on glob referencing some good and one bad file");

    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);
    like (error.filename, "99.*\\.toml",
          "Failed file contained in error.filename");

    /* Invalid pattern arg
     */
    errno = 0;
    ok (conf_parse (NULL, &error) == NULL && errno == EINVAL,
        "conf_parse pattern=NULL fails with EINVAL");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);

    /* Directory not found causes a GLOB_ABORTED error (returned as EINVAL).
     */
    errno = 0;
    ok (conf_parse ("/noexist/*.toml", &error) == NULL && errno == EINVAL,
        "conf_parse pattern=/noexist/*.toml fails with EINVAL");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);

    /* No glob match causes GLOB_NOMATCH error (returned as ENOENT)
     */
    if (snprintf (p, sizeof (p), "%s/*.noexist", dir) >= sizeof (p))
        BAIL_OUT ("snprintf failed in creating toml file pattern");
    errno = 0;
    ok (conf_parse (p, &error) == NULL && errno == ENOENT,
        "conf_parse pattern=*.noexist fails with ENOENT");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);


    if (   (unlink (path1) < 0)
        || (unlink (path2) < 0)
        || (unlink (path3) < 0)
        || (unlink (invalid) < 0) )
        BAIL_OUT ("unlink: %s", strerror (errno));
    if (rmdir (dir) < 0)
        BAIL_OUT ("rmdir: %s: %s", dir, strerror (errno));

}

void test_default_pattern (void)
{
    char toosmall[1];
    char buf[PATH_MAX+1];
    char exp[PATH_MAX+1];
    const char *cf_path;

    /* default
     */
    cf_path = flux_conf_builtin_get ("cf_path", FLUX_CONF_AUTO);
    (void)snprintf (exp, sizeof (exp), "%s/*.toml", cf_path);
    ok (conf_get_default_pattern (buf, sizeof (buf)) == 0
        && !strcmp (buf, exp),
        "conf_get_default_pattern works");

    /* FLUX_CONF_DIR="/a/b"
     */
    setenv ("FLUX_CONF_DIR", "/a/b", 1);
    (void)snprintf (exp, sizeof (exp), "/a/b/*.toml");
    ok (conf_get_default_pattern (buf, sizeof (buf)) == 0
        && !strcmp (buf, exp),
        "conf_get_default_pattern FLUX_CONF_DIR=/a/b works");
    unsetenv ("FLUX_CONF_DIR");

    /* FLUX_CONF_DIR="installed"
     */
    setenv ("FLUX_CONF_DIR", "installed", 1);
    cf_path = flux_conf_builtin_get ("cf_path", FLUX_CONF_INSTALLED);
    (void)snprintf (exp, sizeof (exp), "%s/*.toml", cf_path);
    ok (conf_get_default_pattern (buf, sizeof (buf)) == 0
        && !strcmp (buf, exp),
        "conf_get_default_pattern FLUX_CONF_DIR=installed works");
    unsetenv ("FLUX_CONF_DIR");

    /* Tiny buffer fails
     */
    errno = 0;
    ok (conf_get_default_pattern (toosmall, sizeof (toosmall)) < 0
        && errno == EOVERFLOW,
        "conf_get_default_pattern bufsz=1 failed with EOVERFLOW");

}

void test_in_handle (void)
{
    const char *tmpdir = getenv ("TMPDIR");
    char dir[PATH_MAX + 1];
    char path[PATH_MAX + 1];
    char invalid[PATH_MAX + 1];
    const flux_conf_t *conf;
    flux_conf_error_t error;
    flux_t *h;
    int i;

    /* create test handle
     */
    if (!(h = loopback_create (0)))
        BAIL_OUT ("loopback_create failed");

    /* create test config
     */
    snprintf (dir, sizeof (dir), "%s/cf.XXXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp %s: %s", dir, strerror (errno));
    create_test_file (dir, "foo", path, sizeof (path), t1);

    setenv ("FLUX_CONF_DIR", dir, 1);
    conf = flux_get_conf (h, NULL);
    unsetenv ("FLUX_CONF_DIR");
    ok (conf != NULL,
        "flux_get_conf works");

    /* quick spot check content
     */
    i = 0;
    ok (flux_conf_unpack (conf, NULL, "{s:i}", "i", &i) == 0 && i == 1,
        "and config content is as expected");

    /* add invalid toml, reset handle config, and get again (should fail)
     */
    create_test_file (dir, "99", invalid, sizeof (invalid), "key = \n");
    ok (handle_set_conf (h, NULL) == 0,
        "clearing flux_t handle conf cache works");
    setenv ("FLUX_CONF_DIR", dir, 1);
    conf = flux_get_conf (h, &error);
    unsetenv ("FLUX_CONF_DIR");
    ok (conf == NULL,
        "flux_get_conf fails on bad TOML");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);

    if (unlink (path) < 0 || unlink (invalid) < 0)
        BAIL_OUT ("unlink: %s", strerror (errno));
    if (rmdir (dir) < 0)
        BAIL_OUT ("rmdir: %s: %s", dir, strerror (errno));

    flux_close (h);
}

void test_globerr (void)
{
    flux_conf_error_t error;

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "meep", GLOB_NOMATCH);
    ok (errno == ENOENT
        && !strcmp (error.filename, "meep")
        && !strcmp (error.errbuf, "No match")
        && error.lineno == -1,
        "conf_globerr pat=meep rc=NOMATCH sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "moo", GLOB_NOSPACE);
    ok (errno == ENOMEM
        && !strcmp (error.filename, "moo")
        && !strcmp (error.errbuf, "Out of memory")
        && error.lineno == -1,
        "conf_globerr pat=moo rc=NOSPACE sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "foo", GLOB_ABORTED);
    ok (errno == EINVAL
        && !strcmp (error.filename, "foo")
        && !strcmp (error.errbuf, "Read error")
        && error.lineno == -1,
        "conf_globerr pat=moo rc=ABORTED sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "oops", 666);
    ok (errno == EINVAL
        && !strcmp (error.filename, "oops")
        && !strcmp (error.errbuf, "Unknown glob error")
        && error.lineno == -1,
        "conf_globerr pat=oops rc=666 sets errno and error as expected");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    unsetenv ("FLUX_CONF_DIR");

    test_builtin ();
    test_basic (); // conf_parse(), conf_destroy(), flux_conf_unpack()
    test_default_pattern ();
    test_in_handle ();
    test_globerr ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
