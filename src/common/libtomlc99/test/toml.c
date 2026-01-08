/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
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

#include <stdio.h>
#include <glob.h>
#include <limits.h>
#include <libgen.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <locale.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/basename.h"
#include "toml.h"

#define EX1 "\
[server]\n\
    host = \"www.example.com\"\n\
    port = 80\n\
    verbose = false\n\
    timeout = 1.5E3\n\
"

bool validate_toml_table (toml_table_t *conf, char *errbuf, int errsize);
bool validate_toml_array (toml_array_t *array, char *errbuf, int errsize);
bool validate_toml_value (const char *raw, char *errbuf, int errsize);

void parse_ex1 (void)
{
    char errbuf[255];
    toml_table_t *conf;
    toml_table_t *server;
    const char *raw;
    char *host;
    int64_t port;
    int verbose;
    double timeout;
    int rc;

    conf = toml_parse (EX1, errbuf, sizeof (errbuf));
    ok (conf != NULL,
        "ex1: parsed simple example");

    server = toml_table_in (conf, "server");
    ok (server != NULL,
        "ex1: located server table");

    raw = toml_raw_in (server, "host");
    ok (raw != NULL,
        "ex1: located host in server table");
    host = NULL;
    rc = toml_rtos (raw, &host);
    ok (rc == 0,
        "ex1: extracted host string");
    is (host, "www.example.com",
        "ex1: host string has expected value");

    raw = toml_raw_in (server, "port");
    ok (raw != NULL,
        "ex1: located port in server table");
    port = 0;
    rc = toml_rtoi (raw, &port);
    ok (rc == 0,
        "ex1: extracted port int");
    ok (port == 80,
        "ex1: port int has expected value");

    raw = toml_raw_in (server, "verbose");
    ok (raw != NULL,
        "ex1: located verbose in server table");
    verbose = 2;
    rc = toml_rtob (raw, &verbose);
    ok (rc == 0,
        "ex1: extracted verbose boolean");
    ok (verbose == 0,
        "ex1: verbose boolean has expected value");

    raw = toml_raw_in (server, "timeout");
    ok (raw != NULL,
        "ex1: located timeout in server table");
    timeout = 0;
    rc = toml_rtod (raw, &timeout);
    ok (rc == 0,
        "ex1: extracted timeout double");
    ok (timeout == 1.5E3,
        "ex1: timeout double has expected value");

    toml_free (conf);
    free (host);
}

bool validate_toml_value (const char *raw, char *errbuf, int errsize)
{
    char *str;
    int i;
    int64_t i64;
    double d;
    struct toml_timestamp_t ts;

    if (toml_rtos (raw, &str) == 0) {
        free (str);
        return true;
    }
    else if (toml_rtob (raw, &i) == 0)
        return true;
    else if (toml_rtoi (raw, &i64) == 0)
        return true;
    else if (toml_rtod (raw, &d) == 0)
        return true;
    else if (toml_rtots (raw, &ts) == 0)
        return true;

    snprintf (errbuf, errsize, "%s is an invalid value", raw);
    return false;
}

bool validate_toml_array (toml_array_t *array, char *errbuf, int errsize)
{
    int i;
    const char *raw;
    toml_array_t *arr;
    toml_table_t *tab;

    switch (toml_array_kind (array)) {
        case 'v':
            for (i = 0; (raw = toml_raw_at (array, i)); i++) {
                if (!validate_toml_value (raw, errbuf, errsize))
                    return false;
            }
            break;
        case 'a':
            for (i = 0; (arr = toml_array_at (array, i)); i++) {
                if (!validate_toml_array (arr, errbuf, errsize))
                    return false;
            }
            break;
        case 't':
            for (i = 0; (tab = toml_table_at (array, i)); i++) {
                if (!validate_toml_table (tab, errbuf, errsize))
                    return false;
            }
            break;
    }
    return true;
}

bool validate_toml_table (toml_table_t *conf, char *errbuf, int errsize)
{
    int i;
    const char *key;
    const char *raw;
    toml_array_t *arr;
    toml_table_t *tab;

    for (i = 0; (key = toml_key_in (conf, i)); i++) {
        if ((raw = toml_raw_in (conf, key))) { // value
            if (!validate_toml_value (raw, errbuf, errsize))
                return false;
        }
        else if ((arr = toml_array_in (conf, key))) { // array
            if (!validate_toml_array (arr, errbuf, errsize))
                return false;
        }
        else if ((tab = toml_table_in (conf, key))) { // table
            if (!validate_toml_table (tab, errbuf, errsize))
                return false;
        }
        else {
            snprintf (errbuf, errsize, "key=%s is invalid", key);
            return false;
        }
    }
    return true;
}

/* return true if file can be opened and parsing fails
 */
bool parse_bad_file (const char *path, char *errbuf, int errsize)
{
    FILE *fp;
    toml_table_t *conf = NULL;

    if (!(fp = fopen (path, "r"))) {
        snprintf (errbuf, errsize, "%s", strerror (errno));
        return false;
    }
    conf = toml_parse_file (fp, errbuf, errsize);
    if (conf != NULL) {
        if (validate_toml_table (conf, errbuf, errsize)) {
            toml_free (conf);
            fclose (fp);
            snprintf (errbuf, errsize, "success");
            return false;
        }
        toml_free (conf);
    }
    fclose (fp);
    return true;
}

struct entry {
    char *name;
    char *reason;
};

const struct entry bad_input_blocklist[] = {
    { NULL, NULL },
};

bool matchtab (const char *name, const struct entry tab[], const char **reason)
{
    int i;
    for (i = 0; tab[i].name != NULL; i++)
        if (!strcmp (tab[i].name, name)) {
            *reason = tab[i].reason;
            return true;
        }
    return false;
}

void parse_bad_input (void)
{
    char pattern[PATH_MAX];
    int flags = 0;
    glob_t results;
    unsigned i;

    snprintf (pattern, sizeof (pattern), "%s/*.toml", TEST_BAD_INPUT);
    if (glob (pattern, flags, NULL, &results) != 0)
        BAIL_OUT ("glob %s failed - test input not found", pattern);
    diag ("%d files in %s", results.gl_pathc, TEST_BAD_INPUT);

    for (i = 0; i < results.gl_pathc; i++) {
        char errbuf[255];
        const char *name = basename_simple (results.gl_pathv[i]);
        const char *reason;
        bool blocklist = matchtab (name, bad_input_blocklist, &reason);

        skip (blocklist, 1, "%s: %s", name, reason);
        ok (parse_bad_file (results.gl_pathv[i], errbuf, 255) == true,
            "%s: %s", name, errbuf);
        end_skip;
    }

    globfree (&results);
}

/* return true if file can be opened and parsed
 */
bool parse_good_file (const char *path, char *errbuf, int errsize)
{
    FILE *fp;
    toml_table_t *conf = NULL;


    if (!(fp = fopen (path, "r"))) {
        snprintf (errbuf, errsize, "%s", strerror (errno));
        return false;
    }
    conf = toml_parse_file (fp, errbuf, errsize);
    if (conf == NULL) {
        fclose (fp);
        return false;
    }
    if (!validate_toml_table (conf, errbuf, errsize)) {
        fclose (fp);
        toml_free (conf);
        return false;
    }
    toml_free (conf);
    fclose (fp);
    snprintf (errbuf, errsize, "success");
    return true;
}

void parse_good_input (void)
{
    char pattern[PATH_MAX];
    int flags = 0;
    glob_t results;
    unsigned i;

    snprintf (pattern, sizeof (pattern), "%s/*.toml", TEST_GOOD_INPUT);
    if (glob (pattern, flags, NULL, &results) != 0)
        BAIL_OUT ("glob %s failed - test input not found", pattern);
    diag ("%d files in %s", results.gl_pathc, TEST_GOOD_INPUT);

    for (i = 0; i < results.gl_pathc; i++) {
        char errbuf[255];
        ok (parse_good_file (results.gl_pathv[i], errbuf, 255) == true,
            "%s: %s", basename_simple (results.gl_pathv[i]), errbuf);
    }
    globfree (&results);
}

/* Recreate the TOML input from the tomlc99/test/extra directory.
 */
void parse_extra (void)
{
    const char *good[] = {
        "x = [ {'a'= 1}, {'a'= 2} ]",   // array_of_tables.toml
        "x = [1,2,3]",                  // inline_array.toml
        "x = {'a'= 1, 'b'= 2 }",        // inline_table.toml
        NULL,
    };
    int i;

    for (i = 0; good[i] != NULL; i++) {
        char e[200];
        toml_table_t *conf = toml_parse ((char *)good[i], e, sizeof (e));
        ok (conf != NULL,
            "parsed extra %d: \"%s\"", i, good[i]);
        if (!conf)
            diag ("%s", e);
        toml_free (conf);
    }

}

void check_ucs_to_utf8 (void)
{
    char buf[6];
    int64_t code;
    int errors;

    errors = 0;
    for (code = 0xd800; code <= 0xdfff; code++)
        if (toml_ucs_to_utf8 (code, buf) != -1)
            errors++;
    ok (errors == 0,
        "ucs_to_utf8: UTF-16 surrogates are rejected");

    errors = 0;
    for (code = 0xfffe; code <= 0xffff; code++)
        if (toml_ucs_to_utf8 (code, buf) != -1)
            errors++;
    ok (errors == 0,
        "ucs_to_utf8: UCS non-characters are rejected");

    ok (toml_ucs_to_utf8 (-42, buf) < 0,
        "ucs_to_utf8: UCS negative code is rejected");

    errors = 0;
    for (code = 0; code <= 0x7f; code++)
        if (toml_ucs_to_utf8 (code, buf) != 1 || buf[0] != code)
            errors++;
    ok (errors == 0,
        "ucs_to_utf8: 1 byte codes convert directly to UTF8");

    /* Check boundary values against results from:
     *   http://www.ltg.ed.ac.uk/~richard/utf-8.cgi
     */

    ok (toml_ucs_to_utf8 (0x80, buf) == 2 && !memcmp (buf, "\xc2\x80", 2),
        "ucs_to_utf8: 0x80 converted to 2-char UTF8");
    ok (toml_ucs_to_utf8 (0x7ff, buf) == 2 && !memcmp (buf, "\xdf\xbf", 2),
        "ucs_to_utf8: 0x7ff converted to 2-char UTF8");

    ok (toml_ucs_to_utf8 (0x800, buf) == 3 && !memcmp (buf, "\xe0\xa0\x80", 3),
        "ucs_to_utf8: 0x800 converted to 3-char UTF8");
    ok (toml_ucs_to_utf8 (0xfffd, buf) == 3 && !memcmp (buf, "\xef\xbf\xbd", 3),
        "ucs_to_utf8: 0xfffd converted to 3-char UTF8");

    ok (toml_ucs_to_utf8 (0x10000, buf) == 4
        && !memcmp (buf, "\xf0\x90\x80\x80", 4),
        "ucs_to_utf8: 0x10000 converted to 4-char UTF8");
    ok (toml_ucs_to_utf8 (0x1fffff, buf) == 4
        && !memcmp (buf, "\xf7\xbf\xbf\xbf", 4),
        "ucs_to_utf8: 0x1fffff converted to 4-char UTF8");

    ok (toml_ucs_to_utf8 (0x200000, buf) == 5
        && !memcmp (buf, "\xf8\x88\x80\x80\x80", 5),
        "ucs_to_utf8: 0x200000 converted to 5-char UTF8");
    ok (toml_ucs_to_utf8 (0x3ffffff, buf) == 5
        && !memcmp (buf, "\xfb\xbf\xbf\xbf\xbf", 5),
        "ucs_to_utf8: 0x3ffffff converted to 5-char UTF8");

    ok (toml_ucs_to_utf8 (0x4000000, buf) == 6
        && !memcmp (buf, "\xfc\x84\x80\x80\x80\x80", 6),
        "ucs_to_utf8: 0x4000000 converted to 6-char UTF8");
    ok (toml_ucs_to_utf8 (0x7fffffff, buf) == 6
        && !memcmp (buf, "\xfd\xbf\xbf\xbf\xbf\xbf", 6),
        "ucs_to_utf8: 0x7fffffff converted to 6-char UTF8");
}

void check_utf8_to_ucs (void)
{
    int64_t code;

    /* Reverse above values
     */

    ok (toml_utf8_to_ucs ("\x0", 1, &code) == 1 && code == 0,
        "utf8_to_ucs: 0 converted from 1-char UTF8");
    ok (toml_utf8_to_ucs ("\x7f", 1, &code) == 1 && code == 0x7f,
        "utf8_to_ucs: 0x7f converted from 1-char UTF8");

    ok (toml_utf8_to_ucs ("\xc2\x80", 2, &code) == 2 && code == 0x80,
        "utf8_to_ucs: 0x80 converted from 2-char UTF8");
    ok (toml_utf8_to_ucs ("\xdf\xbf", 2, &code) == 2 && code == 0x7ff,
        "utf8_to_ucs: 0x7ff converted from 2-char UTF8");

    ok (toml_utf8_to_ucs ("\xe0\xa0\x80", 3, &code) == 3 && code == 0x800,
        "utf8_to_ucs: 0x800 converted from 3-char UTF8");
    ok (toml_utf8_to_ucs ("\xef\xbf\xbd", 3, &code) == 3 && code == 0xfffd,
        "utf8_to_ucs: 0xfffd converted from 3-char UTF8");

    ok (toml_utf8_to_ucs ("\xf0\x90\x80\x80", 4, &code) == 4 && code == 0x10000,
        "utf8_to_ucs: 0x10000 converted from 4-char UTF8");
    ok (toml_utf8_to_ucs ("\xf7\xbf\xbf\xbf", 4, &code) == 4 && code == 0x1fffff,
        "utf8_to_ucs: 0x1fffff converted from 4-char UTF8");

    ok (toml_utf8_to_ucs ("\xf8\x88\x80\x80\x80", 5, &code) == 5 && code == 0x200000,
        "utf8_to_ucs: 0x200000 converted from 5-char UTF8");
    ok (toml_utf8_to_ucs ("\xfb\xbf\xbf\xbf\xbf", 5, &code) == 5 && code == 0x3ffffff,
        "utf8_to_ucs: 0x3ffffff converted from 5-char UTF8");

    ok (toml_utf8_to_ucs ("\xfc\x84\x80\x80\x80\x80", 6, &code) == 6 && code == 0x4000000,
        "utf8_to_ucs: 0x4000000 converted from 6-char UTF8");
    ok (toml_utf8_to_ucs ("\xfd\xbf\xbf\xbf\xbf\xbf", 6, &code) == 6 && code == 0x7fffffff,
        "utf8_to_ucs: 0x7fffffff converted from 6-char UTF8");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    parse_ex1 ();

    parse_good_input ();
    parse_bad_input ();

    parse_extra ();

    check_ucs_to_utf8 ();
    check_utf8_to_ucs ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
