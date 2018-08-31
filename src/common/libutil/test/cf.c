#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "cf.h"

const char *t1 = \
"i = 1\n" \
"d = 3.14\n" \
"s = \"foo\"\n" \
"b = true\n" \
"ts = 1979-05-27T07:32:00Z\n" \
"ai = [ 1, 2, 3]\n" \
"[tab]\n" \
"subvalue = 42\n";

const char *tab1 = \
"[tab1]\n" \
"id = 1\n";

const char *tab2 = \
"[tab2]\n" \
"id = 2\n";

const char *tab3 = \
"[tab3]\n" \
"id = 3\n";

const struct cf_option opts[] = {
    { "i", CF_INT64, true },
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    CF_OPTIONS_TABLE_END,
};

const struct cf_option opts_multi[] = {
    { "id", CF_INT64, true },
    CF_OPTIONS_TABLE_END,
};

const struct cf_option opts_combined[] = {
    { "i", CF_INT64, true },
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    { "tab2", CF_TABLE, true },
    { "tab3", CF_TABLE, true },
    CF_OPTIONS_TABLE_END,
};

static time_t strtotime (const char *s)
{
    struct tm tm;
    time_t t;
    if (!strptime (s, "%FT%TZ", &tm))
        BAIL_OUT ("strptime: %s failed", s);
    if ((t = timegm (&tm)) < 0)
        BAIL_OUT ("timegm: %s failed", s);
    return t;
}

void cfdiag (int rc, const char *prefix, struct cf_error *error)
{
    if (rc < 0)
        diag ("%s: %s::%d: %s", prefix,
              error->filename, error->lineno, error->errbuf);
}

void test_basic (void)
{
    cf_t *cf, *cf_cpy;
    const cf_t *cf2;
    struct cf_error error;
    int rc;
    const char *s;
    time_t t;

    /* Create a new cf object.  It's type should be table.
     */
    cf = cf_create ();
    ok (cf != NULL,
        "cf_create works");
    ok (cf_typeof (cf) == CF_TABLE,
        "cf_typeof says empty cf is CF_TABLE");

    /* Copy a cf object.
     */
    cf_cpy = cf_copy (cf);
    ok (cf_cpy != NULL,
        "cf_copy works");
    ok (cf_typeof (cf) == CF_TABLE,
        "cf_typeof says copy is CF_TABLE");
    cf_destroy (cf_cpy);

    /* Read some TOML into the cf top level table
     */
    rc = cf_update (cf, t1, strlen (t1), &error);
    ok (rc == 0,
        "cf_update t1 worked");
    cfdiag (rc, "cf_update t1", &error);

    /* Check the cf object against 'opts'.
     * All keys and their types must be declared in 'opts' (CF_STRICT).
     */
    rc = cf_check (cf, opts, CF_STRICT, &error);
    ok (rc == 0,
        "cf_check t1 worked");
    cfdiag (rc, "cf_check t1", &error);

    /* Access all the simple types and ensure they have the expected values.
     */
    ok (cf_int64 (cf_get_in (cf, "i")) == 1,
        "accessed int64 value");
    ok (cf_double (cf_get_in (cf, "d")) == 3.14,
        "accessed double value");
    s = cf_string (cf_get_in (cf, "s"));
    ok (s != NULL && !strcmp (s, "foo"),
        "accessed string value");
    ok (cf_bool (cf_get_in (cf, "b")) == true,
        "accessed bool value");
    t = cf_timestamp (cf_get_in (cf, "ts"));
    ok (t == strtotime ("1979-05-27T07:32:00Z"),
        "accessed ts value ");
    cf2 = cf_get_in (cf, "ai");

    /* Access array and its elements.
     */
    ok (cf2 && cf_typeof (cf2) == CF_ARRAY,
        "accessed array");
    ok (cf_array_size (cf2) == 3,
        "array has expected size");
    ok (cf_int64 (cf_get_at (cf2, 0)) == 1
        && cf_int64 (cf_get_at (cf2, 1)) == 2
        && cf_int64 (cf_get_at (cf2, 2)) == 3,
        "accessed array elements");

    /* Access sub-table and its key (without cf_check on it first)
     */
    cf2 = cf_get_in (cf, "tab");
    ok (cf2 && cf_typeof (cf2) == CF_TABLE,
        "accessed table");
    ok (cf_int64 (cf_get_in (cf2, "subvalue")) == 42,
        "accessed value in table");

    cf_destroy (cf);
}

void test_multi (void)
{
    cf_t *cf;
    const cf_t *cf2;
    int rc;
    struct cf_error error;

    if (!(cf = cf_create ()))
        BAIL_OUT ("cf_create: %s", strerror (errno));

    /* Combine three TOML config "files" into one cf.
     */
    rc = cf_update (cf, tab1, strlen (tab1), &error);
    ok (rc == 0,
        "cf_update tab1 worked");
    cfdiag (rc, "cf_update tab1", &error);

    rc = cf_update (cf, tab2, strlen (tab2), &error);
    ok (rc == 0,
        "cf_update tab2 worked");
    cfdiag (rc, "cf_update tab2", &error);

    rc = cf_update (cf, tab3, strlen (tab3), &error);
    ok (rc == 0,
        "cf_update tab3 worked");
    cfdiag (rc, "cf_update tab3", &error);

    /* check the cf object against 'opts'.
     * Since opts=NULL, there should be no keys at this level (CF_STRICT)
     * unless they are tables (CF_ANYTAB).
     */
    rc = cf_check (cf, NULL, CF_STRICT | CF_ANYTAB, &error);
    ok (rc == 0,
        "cf_check CF_STRICT|CF_ANYTAB worked");
    cfdiag (rc, "cf_check multi", &error);

    /* Check first subtable against 'opts_multi' and access content.
     */
    cf2 = cf_get_in (cf, "tab1");
    ok (cf2 && cf_typeof (cf2) == CF_TABLE,
        "accessed tab1");
    rc = cf_check (cf2, opts_multi, CF_STRICT, &error);
    ok (rc == 0,
        "cf_check tab1 worked");
    cfdiag (rc, "cf_check tab1", &error);
    ok (cf_int64 (cf_get_in (cf2, "id")) == 1,
        "tab1 id key has correct value");

    /* Check second subtable against 'opts_multi' and access content.
     */
    cf2 = cf_get_in (cf, "tab2");
    ok (cf2 && cf_typeof (cf2) == CF_TABLE,
        "accessed tab2");
    rc = cf_check (cf2, opts_multi, CF_STRICT, &error);
    ok (rc == 0,
        "cf_check tab2 worked");
    cfdiag (rc, "cf_check tab2", &error);
    ok (cf_int64 (cf_get_in (cf2, "id")) == 2,
        "tab2 id key has correct value");

    /* Check third subtable against 'opts_multi' and access content.
     */
    cf2 = cf_get_in (cf, "tab3");
    ok (cf2 && cf_typeof (cf2) == CF_TABLE,
        "accessed tab3");
    rc = cf_check (cf2, opts_multi, CF_STRICT, &error);
    ok (rc == 0,
        "cf_check tab3 worked");
    cfdiag (rc, "cf_check tab3", &error);
    ok (cf_int64 (cf_get_in (cf2, "id")) == 3,
        "tab3 id key has correct value");

    cf_destroy (cf);
}

void test_corner (void)
{
    cf_t *cf;
    const cf_t *cf_array;
    struct cf_error error;

    if (!(cf = cf_create ()))
        BAIL_OUT ("cf_create setup failed");
    const char *toml = "foo = [1,2,3]";
    if (cf_update (cf, toml, strlen (toml), NULL) < 0)
        BAIL_OUT ("cf_update setup failed");
    if (!(cf_array = cf_get_in (cf, "foo")))
        BAIL_OUT ("cf_get_in setup failed ");

    /* cf_check
     */
    errno = 0;
    ok (cf_check (NULL, NULL, 0, &error) < 0 && errno == EINVAL,
         "cf_check cf=NULL fails with EINVAL");
    errno = 0;
    ok (cf_check (cf_array, NULL, 0, &error) < 0 && errno == EINVAL,
         "cf_check cf=(not table) fails with EINVAL");

    /* cf_update
     */
    errno = 0;
    ok (cf_update (NULL, NULL, 0, &error) < 0 && errno == EINVAL,
        "cf_update cf=NULL fails with EINVAL");
    errno = 0;
    ok (cf_update (cf, NULL, 0, &error) == 0,
        "cf_update buf=NULL works (no-op)");
    errno = 0;
    const char *junk = ",]foo";
    ok (cf_update (cf, junk, strlen (junk), &error) < 0 && errno == EINVAL,
        "cf_update buf=\"%s\" fails with EINVAL", junk);

    /* cf_get_in
     */
    errno = 0;
    ok (cf_get_in (NULL, "foo") == NULL && errno == EINVAL,
        "cf_get_in cf=NULL fails with EINVAL");
    errno = 0;
    ok (cf_get_in (cf_array, "foo") == NULL && errno == EINVAL,
        "cf_get_in cf=(not table) fails with EINVAL");
    errno = 0;
    ok (cf_get_in (cf, NULL) == NULL && errno == EINVAL,
        "cf_get_in key=NULL fails with EINVAL");
    errno = 0;
    ok (cf_get_in (cf, "bar") == NULL && errno == ENOENT,
        "cf_get_in key=(unknown) fails wtih ENOENT");

    /* cf_get_at
     */
    errno = 0;
    ok (cf_get_at (NULL, 0) == NULL && errno == EINVAL,
        "cf_get_at cf=NULL fails with EINVAL");
    errno = 0;
    ok (cf_get_at (cf, 0) == NULL && errno == EINVAL,
        "cf_get_at cf=(not array) fails with EINVAL");
    errno = 0;
    ok (cf_get_at (cf_array, -1) == NULL && errno == EINVAL,
        "cf_get_at index=-1 fails with EINVAL");
    errno = 0;
    ok (cf_get_at (cf_array, 4) == NULL && errno == ENOENT,
        "cf_get_at index=(too big) fails with ENOENT");

    /* cf_copy
     */
    errno = 0;
    ok (cf_copy (NULL) == NULL && errno == EINVAL,
        "cf_copy cf=NULL fails with EINVAL");

    /* cf_typeof
     */
    ok (cf_typeof (NULL) == CF_UNKNOWN,
        "cf_typeof cf=NULL returns CF_UNKNOWN");

    /* simple accessors
     */
    ok (cf_int64 (NULL) == 0,
        "cf_int64 cf=NULL returns 0");
    ok (cf_double (NULL) == 0.,
        "cf_double cf=NULL returns 0.");
    ok (strlen (cf_string (NULL)) == 0,
        "cf_string cf=NULL returns \"\"");
    ok (cf_bool (NULL) == false,
        "cf_bool cf=NULL returns false");
    ok (cf_timestamp (NULL) == 0,
        "cf_timestamp cf=NULL returns 0");

    /* cf_array_size
     */
    ok (cf_array_size (NULL) == 0,
        "cf_array_size cf=NULL returns 0");
    ok (cf_array_size (cf) == 0,
        "cf_array_size cf=(not array) returns 0");

    cf_destroy (cf);
}

const struct cf_option opts_extra[] = { // for 't1'
    // { "i", CF_INT64, true },
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    CF_OPTIONS_TABLE_END,
};

const struct cf_option opts_missing[] = { // for 't1'
    { "i", CF_INT64, true },
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    { "smurf", CF_INT64, true },
    CF_OPTIONS_TABLE_END,
};

const struct cf_option opts_optional[] = { // for 't1'
    { "i", CF_INT64, true },
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    { "smurf", CF_INT64, false },
    CF_OPTIONS_TABLE_END,
};

const struct cf_option opts_wrongtype[] = { // for 't1'
    { "i", CF_DOUBLE , true }, // changed type
    { "d", CF_DOUBLE, true },
    { "s", CF_STRING, true },
    { "b", CF_BOOL, true },
    { "ts", CF_TIMESTAMP, true },
    { "ai", CF_ARRAY, true },
    { "tab", CF_TABLE, true },
    CF_OPTIONS_TABLE_END,
};

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


void test_update_file (void)
{
    char path[PATH_MAX + 1];

    cf_t *cf;
    struct cf_error error;

    create_test_file (getenv ("TMPDIR"), "cf", path, sizeof (path), t1);

    if (!(cf = cf_create ()))
        BAIL_OUT ("cf_create: %s", strerror (errno));

    ok (cf_update_file (cf, path, &error) == 0,
        "cf_update_file works");
    errno = 0;
    ok (cf_update_file (cf, "/noexist", &error) < 0 && errno == ENOENT,
        "cf_update_file fails on nonexistent file");

    if (unlink (path) < 0)
        BAIL_OUT ("unlink %s: %s", path, strerror (errno));
    cf_destroy (cf);
}

void test_update_glob (void)
{
    int rc;
    int len;
    const char *tmpdir = getenv ("TMPDIR");
    char dir[PATH_MAX + 1];
    char path1[PATH_MAX + 1];
    char path2[PATH_MAX + 1];
    char path3[PATH_MAX + 1];
    char invalid[PATH_MAX + 1];
    char p [1024];

    cf_t *cf;
    const cf_t *cf2, *cf3;
    struct cf_error error;


    snprintf (dir, sizeof (dir), "%s/cf.XXXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp %s: %s", dir, strerror (errno));

    create_test_file (dir, "01", path1, sizeof (path1), t1);
    create_test_file (dir, "02", path2, sizeof (path2), tab2);
    create_test_file (dir, "03", path3, sizeof (path3), tab3);

    if (!(cf = cf_create ()))
        BAIL_OUT ("cf_create: %s", strerror (errno));

    len = snprintf (p, sizeof (p), "%s/*.toml", dir);
    if ((len < 0) || (len >= sizeof (p)))
        BAIL_OUT ("snprintf failed in creating toml file path");

    ok (cf_update_glob (cf, p, &error) == 3, 
        "cf_update_glob successfully parsed 3 files");

    /* Check the cf object against 'opts'.
     * All keys and their types must be declared in 'opts' (CF_STRICT).
     */
    rc = cf_check (cf, opts_combined, CF_STRICT, &error);
    ok (rc == 0, "cf_check t1 worked");
    cfdiag (rc, "cf_check t1", &error);

    ok ((cf2 = cf_get_in (cf, "tab2")) != NULL,
        "found tab2 table in cf");

    rc = cf_check (cf2, opts_multi, CF_STRICT, &error);
    ok (rc == 0, "cf_check tab2 worked");
    cfdiag (rc, "cf_check tab2", &error);

    ok ((cf3 = cf_get_in (cf, "tab3")) != NULL,
        "found tab3 table in cf");

    errno = 0;
    ok ((cf_update_glob (cf, "/noexist*", &error) == 0),
        "cf_update_glob returns 0 on no match");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);
    like (error.errbuf, "[nN]o [mM]atch", "got expected error text");


    errno = 0;
    ok ((cf_update_glob (cf, "/noexist/*", &error) < 0) && errno == EINVAL,
        "cf_update_glob fails on read error");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);
    like (error.errbuf, "[rR]ead [eE]rror", "got expected error text");

    cf_destroy (cf);

    /* Now make an invalid file and ensure cf_update_glob() aborts
     * all updates after any one failure
     */
    create_test_file (dir, "99", invalid, sizeof (invalid), "key = \n");
    cf = cf_create ();
    ok (cf_update_glob (cf, p, &error) < 0 && errno == EINVAL,
        "cf_update_glob fails when one file fails to parse");
    diag ("%s: %d: %s", error.filename, error.lineno, error.errbuf);
    like (error.filename, "99.*\\.toml",
          "Failed file contained in error.filename");

    ok (cf_get_in (cf, "i") == NULL,
        "keys from ok files not added to cf table when one file fails");

    cf_destroy (cf);

    if (   (unlink (path1) < 0)
        || (unlink (path2) < 0)
        || (unlink (path3) < 0)
        || (unlink (invalid) < 0) )
        BAIL_OUT ("unlink: %s", strerror (errno));
    if (rmdir (dir) < 0)
        BAIL_OUT ("rmdir: %s: %s", dir, strerror (errno));

}

void test_check (void)
{
    cf_t *cf;
    struct cf_error error;
    int rc;

    if (!(cf = cf_create ()))
        BAIL_OUT ("cf_create");
    if (cf_update (cf, t1, strlen (t1), NULL) < 0)
        BAIL_OUT ("cf_update");

    /* Extra key.
     */
    rc = cf_check (cf, opts_extra, 0, &error);
    ok (rc == 0,
        "cf_check flags=0 allows extra key");
    cfdiag (rc, "cf_check", &error);

    errno = 0;
    rc = cf_check (cf, opts_extra, CF_STRICT, &error);
    ok (rc < 0 && errno == EINVAL,
        "cf_check flags=CF_STRICT fails with extra key");
    cfdiag (rc, "cf_check", &error);

    /* Missing key.
     */
    errno = 0;
    rc = cf_check (cf, opts_missing, 0, &error);
    ok (rc < 0 && errno == EINVAL,
        "cf_check fails on missing required key with EINVAL");
    cfdiag (rc, "cf_check", &error);

    rc = cf_check (cf, opts_optional, 0, &error);
    ok (rc == 0,
        "cf_check succeeds on missing optional key");
    cfdiag (rc, "cf_check", &error);

    /* Wrong type
     */
    errno = 0;
    rc = cf_check (cf, opts_wrongtype, 0, &error);
    ok (rc < 0 && errno == EINVAL,
        "cf_check fails on wrong type");
    cfdiag (rc, "cf_check", &error);

    cf_destroy (cf);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_multi ();
    test_corner ();
    test_update_file ();
    test_update_glob ();
    test_check ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
