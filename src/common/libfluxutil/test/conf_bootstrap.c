/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <fcntl.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"
#include "tap.h"

#include "conf_bootstrap.h"

#ifndef MAX_URI
#define MAX_URI 2048
#endif


static char testdir[1024];

void write_file (const char *path, mode_t mode, const char *content)
{
    size_t len = content ? strlen (content) : 0;
    int fd;
    if ((fd = open (path, O_CREAT | O_TRUNC | O_WRONLY, mode)) < 0
        || write_all (fd, content, len) < len
        || close (fd) < 0)
        BAIL_OUT ("%s: %s", path, strerror (errno));
}

void make_testdir (char *path, size_t size, bool keep)
{
    const char *tmpdir = getenv ("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";
    if (snprintf (path, size, "%s/testbootstrap-XXXXXX", tmpdir) >= size)
        BAIL_OUT ("buffer overflow while creating test directory");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create test directory");
    if (keep)
        diag ("%s will be preserved", path);
    else
        cleanup_push_string (cleanup_directory_recursive, path);
}

flux_conf_t *make_config (const char *name, const char *content)
{
    char path[2048];
    flux_conf_t *conf;
    flux_error_t error;

    snprintf (path, sizeof (path), "%s/%s.toml", testdir, name);
    write_file (path, 0644, content);
    if (!(conf = flux_conf_parse (path, &error)))
        BAIL_OUT ("%s: %s", name, error.text);
    return conf;
}

json_t *find_host (json_t *hosts, const char *name, int *rank)
{
    if (hosts) {
        size_t index;
        json_t *entry;
        json_array_foreach (hosts, index, entry) {
            const char *host;
            if (json_unpack (entry, "{s:s}", "host", &host) == 0
                && streq (host, name)) {
                if (rank)
                    *rank = index;
                return entry;
            }
        }
    }
    return NULL;
}

const char *get_string (json_t *obj, const char *name)
{
    const char *val;
    if (json_unpack (obj, "{s:s}", name, &val) < 0)
        return NULL;
    return val;
}

void test_parse (void)
{
    const char *input =
        "[bootstrap]\n"
        "default_port = 42\n"
        "default_bind = \"tcp://en0:%p\"\n"
        "default_connect = \"tcp://x%h:%p\"\n"
        "curve_cert = \"foo\"\n"
        "hosts = [\n"
        "  { host = \"foo0\" },\n"
        "  { host = \"foo[1-62]\" },\n"
        "  { host = \"foo63\" },\n"
        "]\n";
    flux_conf_t *conf = make_config ("test_parse", input);;
    flux_error_t error;
    json_t *hosts = NULL;
    json_t *entry;
    int rank = -1;

    ok (conf_bootstrap_parse (conf, "foo42", NULL, NULL, NULL, &error) == 0,
        "conf_bootstrap_parse works with hosts=NULL")
        || diag ("%s", error.text);
    ok (conf_bootstrap_parse (conf, "foo42", NULL, NULL, &hosts, &error) == 0,
        "conf_bootstrap_parse works with hosts!=NULL")
        || diag ("%s", error.text);
    ok (hosts != NULL,
        "hosts was assigned");
    ok (json_array_size (hosts) == 64,
        "hosts array size matches the configured instance size");

    entry = find_host (hosts, "foo0", &rank);
    ok (entry != NULL && rank == 0,
        "foo0 has rank 0");
    is (get_string (entry, "bind"), "tcp://en0:42",
        "foo0 has expected bind URI");
    is (get_string (entry, "connect"), "tcp://xfoo0:42",
        "foo0 has expected connect URI");

    entry = find_host (hosts, "foo42", &rank);
    ok (entry != NULL && rank == 42,
        "foo42 has rank 42");
    is (get_string (entry, "bind"), "tcp://en0:42",
        "foo42 has expected bind URI");
    is (get_string (entry, "connect"), "tcp://xfoo42:42",
        "foo42 has expected connect URI");

    entry = find_host (hosts, "foo63", &rank);
    ok (entry != NULL && rank == 63,
        "foo63 has rank 63");
    is (get_string (entry, "bind"), "tcp://en0:42",
        "foo63 has expected bind URI");
    is (get_string (entry, "connect"), "tcp://xfoo63:42",
        "foo63 has expected connect URI");

    json_decref (hosts);
    flux_conf_decref (conf);
}

void test_format_uri (void)
{
    char buf[MAX_URI + 1];

    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "abcd", NULL, 0) == 0
        && streq (buf, "abcd"),
        "format: plain string copy works");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "abcd:%p", NULL, 42) == 0
        && streq (buf, "abcd:42"),
        "format: %%p substitution works end string");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "a%pb", NULL, 42) == 0
        && streq (buf, "a42b"),
        "format: %%p substitution works mid string");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "%p:abcd", NULL, 42) == 0
        && streq (buf, "42:abcd"),
        "format: %%p substitution works begin string");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "%h", NULL, 0) == 0
        && streq (buf, "%h"),
        "format: %%h passes through when host=NULL");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "%h", "foo", 0) == 0
        && streq (buf, "foo"),
        "format: %%h substitution works");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "%%", NULL, 0) == 0
        && streq (buf, "%"),
        "format: %%%% literal works");
    ok (conf_bootstrap_format_uri (buf, sizeof (buf), "a%X", NULL, 0) == 0
        && streq (buf, "a%X"),
        "format: unknown token passes through");

    ok (conf_bootstrap_format_uri (buf, 5, "abcd", NULL, 0) == 0
        && streq (buf, "abcd"),
        "format: copy abcd to buf[5] works");
    ok (conf_bootstrap_format_uri (buf, 4, "abcd", NULL, 0) < 0,
        "format: copy abcd to buf[4] fails");

    ok (conf_bootstrap_format_uri (buf, 5, "a%p", NULL, 123) == 0
        && streq (buf, "a123"),
        "format: %%p substitution into exact size buf works");
    ok (conf_bootstrap_format_uri (buf, 4, "a%p", NULL, 123) < 0,
        "format: %%p substitution overflow detected");

    ok (conf_bootstrap_format_uri (buf, 5, "a%h", "abc", 0) == 0
        && streq (buf, "aabc"),
        "format: %%h substitution into exact size buf works");
    ok (conf_bootstrap_format_uri (buf, 4, "a%h", "abc", 0) < 0,
        "format: %%h substitution overflow detected");
}

const char *domvalid[] = {
    "foo",
    "foo42",
    "foo.bar",
    "foo-bar",
};
const char *dominvalid[] = {
    "-foo",
    "foo-",
    "fo:o",
    "foo.-bar",
    "foo.-bar.baz",
};

void test_domain (void)
{
    flux_error_t error;
    for (int i = 0; i < ARRAY_SIZE (domvalid); i++) {
        ok (conf_bootstrap_validate_domain_name (domvalid[i], &error) == 0,
            "%s is a valid hostname",
            domvalid[i])
            || diag ("%s", error.text);
    }
    for (int i = 0; i < ARRAY_SIZE (dominvalid); i++) {
        ok (conf_bootstrap_validate_domain_name (dominvalid[i], &error) < 0,
            "%s is an invalid hostname",
            dominvalid[i])
            && diag ("%s", error.text);
    }
}

const char *urivalid[] = {
    "ipc://foo/bar/baz",
    "tcp://en0",
    "tcp://foo:42",
    "tcp://foo.bar.baz:42",
    "tcp://1.2.3.4:42",
    "tcp://[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:42",
};
const char *uriinvalid[] = {
    "",
    "pgm://xyz",
    "ipc://01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789",
    "ipc://",
    "tcp://",
    "tcp://eth1:*",
    "ipc://foo:*",
};

void test_zmq_uri (void)
{
    flux_error_t error;
    for (int i = 0; i < ARRAY_SIZE (urivalid); i++) {
        ok (conf_bootstrap_validate_zmq_uri (urivalid[i], &error) == 0,
            "%s is a valid URI",
            urivalid[i])
            || diag ("%s", error.text);
    }
    for (int i = 0; i < ARRAY_SIZE (uriinvalid); i++) {
        ok (conf_bootstrap_validate_zmq_uri (uriinvalid[i], &error) < 0,
            "%s is an invalid URI",
            uriinvalid[i])
            && diag ("%s", error.text);
    }
}

void test_uri_overflow (const char *key)
{
    flux_conf_t *conf;
    char input[4096];
    flux_error_t error;
    char name[128];

    if (snprintf (input,
                  sizeof (input),
                  "[bootstrap]\n"
                  "default_%s=\"ipc://%*s\"\n"
                  "hosts = [{host=\"foo\"}]\n",
                  key,
                  MAX_URI,
                  "xyz") >= sizeof (input))
        BAIL_OUT ("snprintf overflow");
    snprintf (name, sizeof (name), "%s_overflow", key);
    conf = make_config (name, input);

    err_init (&error);
    ok (conf_bootstrap_parse (conf, "foo", NULL, NULL, NULL, &error) == -1,
        "conf_bootstrap_parse caught %s overflow", key)
        && diag ("%s", error.text);

    flux_conf_decref (conf);
}

void test_bad_hosts_entry (const char *entry_input, const char *testname)
{
    flux_conf_t *conf;
    char input[1024];
    char name[128];
    flux_error_t error;
    static int testnum = 0;

    snprintf (name, sizeof (name), "bad_hosts_entry_%d", testnum++);
    snprintf (input,
              sizeof (input),
              "[bootstrap]\n"
              "hosts = [\n"
              "%s,\n"
              "]\n",
              entry_input);
    conf = make_config (name, input);

    err_init (&error);
    ok (conf_bootstrap_parse (conf, "foo", NULL, NULL, NULL, &error) == -1,
        "conf_bootstrap_parse fails on %s", testname)
        && diag ("%s", error.text);

    flux_conf_decref (conf);
}

void test_special_single (const char *hosts_input, const char *testname)
{
    flux_conf_t *conf;
    char input[1024];
    char name[128];
    flux_error_t error;
    json_t *hosts = NULL;
    int rank;
    static int testnum = 0;

    snprintf (name, sizeof (name), "special_single_%d", testnum++);
    snprintf (input,
              sizeof (input),
              "[bootstrap]\n"
              "%s\n",
              hosts_input);
    conf = make_config (testname, input);

    ok (conf_bootstrap_parse (conf, "smurf", NULL, NULL, &hosts, &error) == 0,
        "conf_bootstrap_parse accepts %s", testname)
        || diag (error.text);

    ok (find_host (hosts, "smurf", &rank) != NULL
        && rank == 0
        && json_array_size (hosts) == 1,
        "singleton hosts array was generated");

    // N.B. curve cert is not required for singleton

    json_decref (hosts);
    flux_conf_decref (conf);
}

void test_missing_cert (void)
{
    const char *input =
        "[bootstrap]\n"
        "default_bind = \"tcp://foo\"\n"
        "default_connect = \"tcp://foo\"\n"
        "hosts = [\n"
        "  { host = \"foo[0-1]\" },\n"
        "]\n";
    flux_conf_t *conf = make_config ("missing_cert", input);
    flux_error_t error;

    ok (conf_bootstrap_parse (conf, "foo0", NULL, NULL, NULL, &error) < 0,
        "conf_bootstrap_parse fails with missing curve_cert on size=2")
        && diag ("%s", error.text);

    flux_conf_decref (conf);
}

void test_dup_hosts (void)
{
    const char *input =
        "[bootstrap]\n"
        "curve_cert = \"foo\"\n"
        "[[bootstrap.hosts]]\n"
        "host = \"test[0-127]\"\n"
        "[[bootstrap.hosts]]\n"
        "host = \"test[1,64]\"\n"
        "parent = \"test0\"\n"
        "[[bootstrap.hosts]]\n"
        "host = \"test[2-63]\"\n"
        "parent = \"test1\"\n"
        "[[bootstrap.hosts]]\n"
        "host = \"test[65-127]\"\n"
        "parent = \"test64\"\n";
    flux_conf_t *conf = make_config ("dup_hosts", input);
    json_t *hosts;
    flux_error_t error;
    int rank;

    ok (conf_bootstrap_parse (conf, "test0", NULL, NULL, &hosts, &error) == 0,
        "conf_bootstrap_parse handles duplicate hosts")
        || diag ("%s", error.text);
    ok (hosts != NULL && json_array_size (hosts) == 128,
        "and hosts array has expected size")
        || diag ("size=%zu", json_array_size (hosts));
    ok (find_host (hosts, "test0", &rank) != NULL && rank == 0,
        "test0 has rank 0");
    ok (find_host (hosts, "test65", &rank) != NULL && rank == 65,
        "test65  has rank 65");
    ok (find_host (hosts, "test127", &rank) != NULL && rank == 127,
        "test127 has rank 127");

    json_decref (hosts);
    flux_conf_decref (conf);
}

int main (int argc, char *argv[])
{
    bool keep = false;

    plan (NO_PLAN);

    if (argc > 1 && streq (argv[1], "keep"))
        keep = true;

    make_testdir (testdir, sizeof (testdir), keep);

    test_format_uri ();
    test_domain ();
    test_zmq_uri ();
    test_parse ();

    test_uri_overflow ("bind");
    test_uri_overflow ("connect");

    test_bad_hosts_entry ("  42", "wrong host key type");
    test_bad_hosts_entry ("\"foo\"", "wrong host key type");
    test_bad_hosts_entry ("{}", "no host key");
    test_bad_hosts_entry ("{ host=\"foo[1-\" }", "bad hostlist");
    test_bad_hosts_entry ("{ host=\"foo\", bind=42 }", "wrong bind key type");
    test_bad_hosts_entry ("{ host=\"foo\", wrongkey=0 }", "extra key");
    test_bad_hosts_entry ("{ host=\"bar\" }", "missing local hostname");
    test_bad_hosts_entry ("{ host=\"foo\", bind=\"x://z\" }",
                          "unknown bind scheme");
    test_bad_hosts_entry ("{ host=\"foo\", connect=\"x://z\" }",
                          "unknown connect scheme");
    test_bad_hosts_entry ("{ host=\"foo\", connect=\"\" }",
                          "empty connect string");
    test_bad_hosts_entry ("{ host=\"foo\", connect=\"ipc://foo:*\" }",
                          "wildcard connect string");
    test_bad_hosts_entry ("{ host=\"foo\", parent=\"foo\" }",
                          "parent is self");
    test_bad_hosts_entry ("{ host=\"foo\", parent=\"woo\" }",
                          "parent is unknown");

    test_special_single ("", "missing hosts array");
    test_special_single ("hosts = []", "empty hosts array");

    test_missing_cert ();
    test_dup_hosts();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
