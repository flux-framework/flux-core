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
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtomlc99/toml.h"
#include "tomltk.h"

/* simple types only */
const char *t1 =
    "i = 1\n"
    "d = 3.14\n"
    "s = \"foo\"\n"
    "b = true\n"
    "ts = 1979-05-27T07:32:00Z\n";

/* table and array */
const char *t2 =
    "[t]\n"
    "ia = [1, 2, 3]\n";

/* sub-table and value */
const char *t3 =
    "[t]\n"
    "[t.a]\n"
    "i = 42\n";

/* bad on line 4 */
const char *bad1 =
    "# line 1\n"
    "# line 2\n"
    "# line 3\n"
    "'# line 4 <- unbalanced tic\n"
    "# line 5\n";

static void jdiag (const char *prefix, json_t *obj)
{
    char *s = json_dumps (obj, JSON_INDENT (2));
    if (!s)
        BAIL_OUT ("json_dumps: %s", strerror (errno));
    diag ("%s: %s", prefix, s);
    free (s);
}

/* Check whether json object represents to ISO 8601 time string.
 */
static bool check_ts (json_t *ts, const char *timestr)
{
    time_t t;
    struct tm tm;
    char buf[80];

    if (tomltk_json_to_epoch (ts, &t) < 0)
        return false;
    if (!gmtime_r (&t, &tm))
        return false;
    if (strftime (buf, sizeof (buf), "%FT%TZ", &tm) == 0)
        return false;
    diag ("%s: %s ?= %s", __FUNCTION__, buf, timestr);
    return !strcmp (buf, timestr);
}

void test_json_ts (void)
{
    time_t t, t2;
    json_t *obj;

    /* Encode the current time, then decode and ensure it matches.
     */
    if (time (&t) < 0)
        BAIL_OUT ("time: %s", strerror (errno));
    obj = tomltk_epoch_to_json (t);
    ok (obj != NULL, "tomltk_epoch_to_json works");

    ok (tomltk_json_to_epoch (obj, &t2) == 0 && t == t2,
        "tomltk_json_to_epoch works, correct value");
    json_decref (obj);
}

void test_tojson_t1 (void)
{
    toml_table_t *tab;
    json_t *obj;
    json_int_t i;
    double d;
    const char *s;
    json_t *ts;
    int b;
    int rc;
    struct tomltk_error error;

    tab = tomltk_parse (t1, strlen (t1), &error);
    ok (tab != NULL, "t1: tomltk_parse works");
    if (!tab)
        BAIL_OUT ("t1: parse error line %d: %s", error.lineno, error.errbuf);

    obj = tomltk_table_to_json (tab);
    ok (obj != NULL, "t1: tomltk_table_to_json works");
    jdiag ("t1", obj);
    rc = json_unpack (obj,
                      "{s:I s:f s:s s:b s:o}",
                      "i",
                      &i,
                      "d",
                      &d,
                      "s",
                      &s,
                      "b",
                      &b,
                      "ts",
                      &ts);
    ok (rc == 0, "t1: unpack successful");
    ok (i == 1 && d == 3.14 && s != NULL && !strcmp (s, "foo") && b != 0
            && check_ts (ts, "1979-05-27T07:32:00Z"),
        "t1: has expected values");
    json_decref (obj);
    toml_free (tab);
}

void test_tojson_t2 (void)
{
    toml_table_t *tab;
    json_t *obj;
    json_int_t ia[3];
    int rc;
    struct tomltk_error error;

    tab = tomltk_parse (t2, strlen (t2), &error);
    ok (tab != NULL, "t2: tomltk_parse works");
    if (!tab)
        BAIL_OUT ("t2: parse error line %d: %s", error.lineno, error.errbuf);

    obj = tomltk_table_to_json (tab);
    ok (obj != NULL, "t2: tomltk_table_to_json works");
    jdiag ("t2", obj);
    rc = json_unpack (obj, "{s:{s:[I,I,I]}}", "t", "ia", &ia[0], &ia[1], &ia[2]);
    ok (rc == 0, "t2: unpack successful");
    ok (ia[0] == 1 && ia[1] == 2 && ia[2] == 3, "t2: has expected values");
    json_decref (obj);
    toml_free (tab);
}

void test_tojson_t3 (void)
{
    toml_table_t *tab;
    json_t *obj;
    json_int_t i;
    int rc;
    struct tomltk_error error;

    tab = tomltk_parse (t3, strlen (t3), &error);
    ok (tab != NULL, "t3: tomltk_parse works");
    if (!tab)
        BAIL_OUT ("t3: parse error line %d: %s", error.lineno, error.errbuf);

    obj = tomltk_table_to_json (tab);
    ok (obj != NULL, "t3: tomltk_table_to_json works");
    jdiag ("t3", obj);
    rc = json_unpack (obj, "{s:{s:{s:I}}}", "t", "a", "i", &i);
    ok (rc == 0, "t3: unpack successful");
    ok (i == 42, "t3: has expected values");
    json_decref (obj);
    toml_free (tab);
}

void test_parse_lineno (void)
{
    toml_table_t *tab;
    struct tomltk_error error;

    errno = 0;
    tab = tomltk_parse (bad1, strlen (bad1), &error);
    if (!tab)
        diag ("filename='%s' lineno=%d msg='%s'",
              error.filename,
              error.lineno,
              error.errbuf);
    ok (tab == NULL && errno == EINVAL, "bad1: parse failed");
    ok (strlen (error.filename) == 0, "bad1: error.filename is \"\"");
    ok (error.lineno == 4, "bad1: error.lineno is 4");
    const char *msg = "unterminated s-quote";
    ok (!strcmp (error.errbuf, msg),
        "bad1: error is \"%s\"",
        msg);  // no "line %d: " prefix
}

void test_corner (void)
{
    time_t t;
    json_t *obj;

    if (!(obj = tomltk_epoch_to_json (time (NULL))))
        BAIL_OUT ("tomltk_epoch_to_json now: %s", strerror (errno));

    errno = 0;
    ok (tomltk_parse_file (NULL, NULL) == NULL && errno == EINVAL,
        "tomltk_parse_file filename=NULL fails with EINVAL");
    errno = 0;
    ok (tomltk_parse ("foo", -1, NULL) == NULL && errno == EINVAL,
        "tomltk_parse len=-1 fails with EINVAL");
    errno = 0;
    ok (tomltk_table_to_json (NULL) == NULL && errno == EINVAL,
        "tomltk_table_to_json NULL fails with EINVAL");

    errno = 0;
    ok (tomltk_json_to_epoch (NULL, &t) < 0 && errno == EINVAL,
        "tomltk_json_to_epoch obj=NULL fails with EINVAL");

    errno = 0;
    ok (tomltk_ts_to_epoch (NULL, NULL) < 0 && errno == EINVAL,
        "tomltk_ts_to_epoch ts=NULL fails with EINVAL");

    errno = 0;
    ok (tomltk_epoch_to_json (-1) == NULL && errno == EINVAL,
        "tomltk_epoch_to_json t=-1 fails with EINVAL");

    errno = 0;
    ok (tomltk_parse_file (NULL, NULL) == NULL && errno == EINVAL,
        "tomltk_parse_file filename=NULL fails with EINVAL");
    errno = 0;
    ok (tomltk_parse_file ("/noexist", NULL) == NULL && errno == ENOENT,
        "tomltk_parse_file filename=(noexist) fails with ENOENT");

    json_decref (obj);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_json_ts ();
    test_tojson_t1 ();
    test_tojson_t2 ();
    test_tojson_t3 ();
    test_parse_lineno ();
    test_corner ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
