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
#include "ccan/str/str.h"

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

const char *tab3_json = \
"{\"tab3\": {\"id\": 4}}";

const char *tab4 = \
"[tab]\n" \
"added = \"bar\"";


static void
create_test_file (const char *dir,
                  char *prefix,
                  char *ext,
                  char *path,
                  size_t pathlen,
                  const char *contents)
{
    int fd;
    snprintf (path,
              pathlen,
              "%s/%s.XXXXXX.%s",
              dir ? dir : "/tmp",
              prefix,
              ext);
    fd = mkstemps (path, 5);
    if (fd < 0)
        BAIL_OUT ("mkstemp %s: %s", path, strerror (errno));
    if (write (fd, contents, strlen (contents)) != strlen (contents))
        BAIL_OUT ("write %s: %s", path, strerror (errno));
    if (close (fd) < 0)
        BAIL_OUT ("close %s: %s", path, strerror (errno));
    diag ("created %s", path);
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
    ok (s2 && s3 && streq (s2, s3),
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
    const char *tmpdir = getenv ("TMPDIR");
    char dir[PATH_MAX + 1];
    char path1[PATH_MAX + 1];
    char path2[PATH_MAX + 1];
    char path3[PATH_MAX + 1];
    char path4[PATH_MAX + 1];
    char pathj[PATH_MAX + 1];
    char invalid[PATH_MAX + 1];
    flux_error_t error;
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

    /* Empty directory is allowed
     */
    conf = flux_conf_parse (dir, &error);
    ok (conf != NULL,
        "flux_conf_parse successfully parsed empty directory");
    flux_conf_decref (conf);

    /* Add files
     */
    create_test_file (dir, "01", "toml", path1, sizeof (path1), t1);
    create_test_file (dir, "02", "toml", path2, sizeof (path2), tab2);
    create_test_file (dir, "03", "toml", path3, sizeof (path3), tab3);
    create_test_file (dir, "04", "toml", path4, sizeof (path4), tab4);
    create_test_file (NULL, "03", "json", pathj, sizeof (pathj), tab3_json);

    /* Parse of one file works
     */
    conf = flux_conf_parse (path3, &error);
    ok (conf != NULL,
        "flux_conf_parse successfully parsed a single file");
    if (!conf)
        BAIL_OUT ("cannot continue without config object");

    /* Check table from path3 toml file
     */
    i = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:i}}",
                           "tab3",
                           "id", &i);
    ok (rc == 0 && i == 3,
        "unpacked integer from [tab3] and got expected value");

    flux_conf_decref (conf);

    /* Parse one file JSON edition
     */
    conf = flux_conf_parse (pathj, &error);
    ok (conf != NULL,
        "flux_conf_parse works for just one file (JSON)");
    i = 0;
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:i}}",
                           "tab3",
                           "id", &i);
    ok (rc == 0 && i == 4,
        "unpacked integer from [tab3] and got expected value");
    flux_conf_decref (conf);

    /* Parse it
     */
    conf = flux_conf_parse (dir, &error);
    ok (conf != NULL,
        "flux_conf_parse successfully parsed 3 files");
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
    ok (s != NULL && streq (s, "foo"),
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

    /* Check that tab was updated with added value from tab4
     */
    rc = flux_conf_unpack (conf,
                           &error,
                           "{s:{s:s}}",
                           "tab",
                           "added", &s);
    diag ("added = %s", s);
    ok (rc == 0 && streq (s, "bar"),
        "unpacked added string from [tab] and got expected value");


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
    ok (strstr (error.text, "noexist") != NULL,
        "and error.text mentions noexist");
    diag ("%s", error.text);

    /* Bad args fail with EINVAL
     */
    errno = 0;
    ok (flux_conf_unpack (NULL, &error, "{s:i}", "i", &i) < 0
        && errno == EINVAL,
        "flux_conf_unpack conf=NULL fails with EINVAL");

    flux_conf_decref (conf);

    /* Now make an invalid file and ensure cf_update_glob() aborts
     * all updates after any one failure
     */
    create_test_file (dir, "99", "toml", invalid, sizeof (invalid), "key = \n");

    conf = flux_conf_parse (invalid, &error);
    ok (conf == NULL,
        "flux_conf_parse failed on bad individual file");
    like (error.text, "99.*\\.toml",
          "Failed file contained in error.text");

    conf = flux_conf_parse (dir, &error);
    ok (conf == NULL,
        "flux_conf_parse choked on glob referencing some good and one bad file");

    diag ("%s", error.text);
    like (error.text, "99.*\\.toml",
          "Failed file contained in error.text");

    /* Parse invalid JSON file
     */
    unlink (invalid);
    create_test_file (dir, "foo", "json", invalid, sizeof (invalid), "{");
    conf = flux_conf_parse (invalid, &error);
    ok (conf == NULL,
        "flux_conf_parse choked on bad file");

    diag ("%s", error.text);
    like (error.text, "foo.*\\.json",
          "Failed file contained in error.text");

    /* Invalid pattern arg
     */
    errno = 0;
    ok (flux_conf_parse (NULL, &error) == NULL && errno == EINVAL,
        "flux_conf_parse path=NULL fails with EINVAL");
    diag ("%s", error.text);

    /* Directory not found triggers ENOENT error
     */
    errno = 0;
    ok (flux_conf_parse ("/noexist", &error) == NULL && errno == ENOENT,
        "flux_conf_parse pattern=/noexist fails with ENOENT");
    diag ("%s", error.text);

    if (   (unlink (path1) < 0)
        || (unlink (path2) < 0)
        || (unlink (path3) < 0)
        || (unlink (path4) < 0)
        || (unlink (pathj) < 0)
        || (unlink (invalid) < 0) )
        BAIL_OUT ("unlink: %s", strerror (errno));
    if (rmdir (dir) < 0)
        BAIL_OUT ("rmdir: %s: %s", dir, strerror (errno));

}

void test_in_handle (void)
{
    const char *tmpdir = getenv ("TMPDIR");
    char dir[PATH_MAX + 1];
    char path[PATH_MAX + 1];
    flux_conf_t *conf;
    flux_t *h;
    int i;

    /* create test handle
     */
    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("failed to create loop handle");

    /* create test config
     */
    snprintf (dir, sizeof (dir), "%s/cf.XXXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp %s: %s", dir, strerror (errno));
    create_test_file (dir, "foo", "toml", path, sizeof (path), t1);
    if (!(conf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failure: %s", strerror (errno));
    ok (flux_set_conf (h, conf) == 0,
        "flux_set_conf works");
    ok (flux_get_conf (h) == conf,
        "flux_get_conf works");

    /* quick spot check content
     */
    i = 0;
    ok (flux_conf_unpack (conf, NULL, "{s:i}", "i", &i) == 0 && i == 1,
        "and config content is as expected");

    ok (flux_set_conf (h, NULL) == 0,
        "flux_set_conf conf=NULL works");
    ok (flux_get_conf (h) == NULL,
        "flux_get_conf now returns NULL");

    if (unlink (path) < 0)
        BAIL_OUT ("unlink: %s", strerror (errno));
    if (rmdir (dir) < 0)
        BAIL_OUT ("rmdir: %s: %s", dir, strerror (errno));

    flux_close (h);
}

void test_globerr (void)
{
    flux_error_t error;

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "meep", GLOB_NOMATCH);
    ok (errno == ENOENT
        && streq (error.text, "meep: No match"),
        "conf_globerr pat=meep rc=NOMATCH sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "moo", GLOB_NOSPACE);
    ok (errno == ENOMEM
        && streq (error.text, "moo: Out of memory"),
        "conf_globerr pat=moo rc=NOSPACE sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "foo", GLOB_ABORTED);
    ok (errno == EINVAL
        && streq (error.text, "foo: Read error"),
        "conf_globerr pat=moo rc=ABORTED sets errno and error as expected");

    errno = 0;
    memset (&error, 0, sizeof (error));
    conf_globerr (&error, "oops", 666);
    ok (errno == EINVAL
        && streq (error.text, "oops: Unknown glob error"),
        "conf_globerr pat=oops rc=666 sets errno and error as expected");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    unsetenv ("FLUX_CONF_DIR");

    test_builtin ();
    test_basic (); // flux_conf_parse(), flux_conf_decref(), flux_conf_unpack()
    test_in_handle ();
    test_globerr ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
