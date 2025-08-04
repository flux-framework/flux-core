/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/test_file.h"
#include "src/broker/boot_config.h"
#include "ccan/str/str.h"


void test_parse (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    json_t *hosts = NULL;
    struct boot_conf conf;
    uint32_t rank;
    char uri[MAX_URI + 1];
    int rc;
    const char *input = \
"[bootstrap]\n" \
"default_port = 42\n" \
"default_bind = \"tcp://en0:%p\"\n" \
"default_connect = \"tcp://x%h:%p\"\n" \
"curve_cert = \"foo\"\n" \
"hosts = [\n" \
"  { host = \"foo0\" },\n" \
"  { host = \"foo[1-62]\" },\n" \
"  { host = \"foo63\" },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");
    rc = boot_config_parse (cf, &conf, &hosts);
    ok (rc == 0,
        "boot_conf_parse worked");
    ok (hosts != NULL && json_array_size (hosts) == 64,
        "got 64 hosts");
    if (hosts == NULL)
        BAIL_OUT ("cannot continue without hosts array");

    ok (conf.default_port == 42,
        "set default_port correctly");
    ok (streq (conf.default_bind, "tcp://en0:42"),
        "and set default_bind correctly (with %%p substitution)");
    ok (streq (conf.default_connect, "tcp://x%h:42"),
        "and set default_connect correctly (with %%p substitution)");

    ok (boot_config_getrankbyname (hosts, "foo0", &rank) == 0
        && rank == 0,
       "boot_config_getrankbyname found rank 0");
    ok (boot_config_getrankbyname (hosts, "foo1", &rank) == 0
        && rank == 1,
       "boot_config_getrankbyname found rank 1");
    ok (boot_config_getrankbyname (hosts, "foo42", &rank) == 0
        && rank == 42,
       "boot_config_getrankbyname found rank 42");
    ok (boot_config_getrankbyname (hosts, "notfound", &rank) < 0,
       "boot_config_getrankbyname fails on unknown entry");

    ok (boot_config_getbindbyrank (hosts, &conf, 0, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://en0:42"),
        "boot_config_getbindbyrank 0 works with expected value");
    ok (boot_config_getbindbyrank (hosts, &conf, 1, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://en0:42"),
        "boot_config_getbindbyrank 1 works with expected value");
    ok (boot_config_getbindbyrank (hosts, &conf, 63, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://en0:42"),
        "boot_config_getbindbyrank 63 works with expected value");
    ok (boot_config_getbindbyrank (hosts, &conf, 64, uri, sizeof (uri))  < 0,
        "boot_config_getbindbyrank 64 fails");

    ok (boot_config_geturibyrank (hosts, &conf, 0, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://xfoo0:42"),
        "boot_config_geturibyrank 0 works with expected value");
    ok (boot_config_geturibyrank (hosts, &conf, 1, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://xfoo1:42"),
        "boot_config_geturibyrank 1 works with expected value");
    ok (boot_config_geturibyrank (hosts, &conf, 63, uri, sizeof (uri)) == 0
        && streq (uri, "tcp://xfoo63:42"),
        "boot_config_geturibyrank 63 works with expected value");
    ok (boot_config_geturibyrank (hosts, &conf, 64, uri, sizeof (uri))  < 0,
        "boot_config_geturibyrank 64 fails");

    json_decref (hosts);

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);
    flux_conf_decref (cf);
}

void test_overflow_bind (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    char t[MAX_URI*2];
    json_t *hosts;

    if (snprintf (t,
                  sizeof (t),
                  "[bootstrap]\ndefault_bind=\"%*s\"\nhosts=[\"foo\"]\n",
                  MAX_URI+2, "foo") >= sizeof (t))
        BAIL_OUT ("snprintf overflow");
    create_test_file (dir, "boot", "toml", path, sizeof (path), t);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");
    ok (boot_config_parse (cf, &conf, &hosts) == -1,
        "boot_conf_parse caught default_bind overflow");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_overflow_connect (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    char t[MAX_URI*2];
    json_t *hosts;

    if (snprintf (t,
                  sizeof (t),
                  "[bootstrap]\ndefault_connect=\"%*s\"\nhosts=[\"foo\"]\n",
                  MAX_URI+2, "foo") >= sizeof (t))
        BAIL_OUT ("snprintf overflow");
    create_test_file (dir, "boot", "toml", path, sizeof (path), t);

    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");
    ok (boot_config_parse (cf, &conf, &hosts) == -1,
        "boot_conf_parse caught default_connect overflow");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_bad_hosts_entry (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    json_t *hosts;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  42,\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);

    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");
    ok (boot_config_parse (cf, &conf, &hosts) == -1,
        "boot_config_parse failed bad hosts entry");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_missing_info (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    json_t *hosts;
    struct boot_conf conf;
    char uri[MAX_URI + 1];
    uint32_t rank;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  { host = \"foo\" },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    if (boot_config_parse (cf, &conf, &hosts) < 0)
        BAIL_OUT ("boot_config_parse unexpectedly failed");
    if (!hosts)
        BAIL_OUT ("cannot continue without hosts array");
    ok (boot_config_getrankbyname (hosts, "foo", &rank) == 0
        && rank == 0,
        "boot_config_getrankbyname found entry");
    ok (boot_config_getbindbyrank (hosts, &conf, 0, uri, sizeof(uri)) < 0,
        "boot_config_getbindbyrank fails due to missing bind uri");
    ok (boot_config_geturibyrank (hosts, &conf, 0, uri, sizeof(uri)) < 0,
        "boot_config_geturibyrank fails due to missing connect uri");

    json_decref (hosts);

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_bad_host_hostlist (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    json_t *hosts;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  { host=\"foo[1-\" },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) == -1,
        "boot_config_parse failed on host entry containing bad idset");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_bad_host_bind (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    json_t *hosts;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  { host=\"foo\", bind=42 },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) < 0,
        "boot_config_parse failed on host entry with wrong bind type");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_bad_host_key (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    struct boot_conf conf;
    json_t *hosts;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  { host=\"foo\", wrongkey=42 },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) < 0,
        "boot_config_parse failed on host entry with unknown key");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

/* Just double check that an array with mismatched types
 * fails early with the expected libtomlc99 error.
 */
void test_toml_mixed_array (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    flux_error_t error;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"  \"bar\",\n" \
"  { host = \"foo\" },\n" \
"]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);

    cf = flux_conf_parse (dir, &error);
    ok (cf == NULL && (strstr (error.text, "array type mismatch")
        || strstr (error.text, "string array can only contain strings")),
        "Mixed type hosts array fails with reasonable error");
    diag ("%s", error.text);

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_empty (const char *dir)
{
    char path[PATH_MAX + 1];
    json_t *hosts;
    flux_conf_t *cf;
    struct boot_conf conf;
    const char *input = \
"[bootstrap]\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    hosts = (json_t *)(uintptr_t)1;
    ok (boot_config_parse (cf, &conf, &hosts) == 0 && hosts == NULL,
        "boot_config_parse works with empty bootstrap stanza");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_empty_hosts (const char *dir)
{
    char path[PATH_MAX + 1];
    json_t *hosts;
    flux_conf_t *cf;
    struct boot_conf conf;
    const char *input = \
"[bootstrap]\n" \
"hosts = [\n" \
"]\n" \
;

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    hosts = (json_t *)(uintptr_t)1;
    ok (boot_config_parse (cf, &conf, &hosts) == 0 && hosts == NULL,
        "boot_config_parse works with empty hosts array");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_format (void)
{
    char buf[MAX_URI + 1];

    ok (boot_config_format_uri (buf, sizeof (buf), "abcd", NULL, 0) == 0
        && streq (buf, "abcd"),
        "format: plain string copy works");
    ok (boot_config_format_uri (buf, sizeof (buf), "abcd:%p", NULL, 42) == 0
        && streq (buf, "abcd:42"),
        "format: %%p substitution works end string");
    ok (boot_config_format_uri (buf, sizeof (buf), "a%pb", NULL, 42) == 0
        && streq (buf, "a42b"),
        "format: %%p substitution works mid string");
    ok (boot_config_format_uri (buf, sizeof (buf), "%p:abcd", NULL, 42) == 0
        && streq (buf, "42:abcd"),
        "format: %%p substitution works begin string");
    ok (boot_config_format_uri (buf, sizeof (buf), "%h", NULL, 0) == 0
        && streq (buf, "%h"),
        "format: %%h passes through when host=NULL");
    ok (boot_config_format_uri (buf, sizeof (buf), "%h", "foo", 0) == 0
        && streq (buf, "foo"),
        "format: %%h substitution works");
    ok (boot_config_format_uri (buf, sizeof (buf), "%%", NULL, 0) == 0
        && streq (buf, "%"),
        "format: %%%% literal works");
    ok (boot_config_format_uri (buf, sizeof (buf), "a%X", NULL, 0) == 0
        && streq (buf, "a%X"),
        "format: unknown token passes through");

    ok (boot_config_format_uri (buf, 5, "abcd", NULL, 0) == 0
        && streq (buf, "abcd"),
        "format: copy abcd to buf[5] works");
    ok (boot_config_format_uri (buf, 4, "abcd", NULL, 0) < 0,
        "format: copy abcd to buf[4] fails");

    ok (boot_config_format_uri (buf, 5, "a%p", NULL, 123) == 0
        && streq (buf, "a123"),
        "format: %%p substitution into exact size buf works");
    ok (boot_config_format_uri (buf, 4, "a%p", NULL, 123) < 0,
        "format: %%p substitution overflow detected");

    ok (boot_config_format_uri (buf, 5, "a%h", "abc", 0) == 0
        && streq (buf, "aabc"),
        "format: %%h substitution into exact size buf works");
    ok (boot_config_format_uri (buf, 4, "a%h", "abc", 0) < 0,
        "format: %%h substitution overflow detected");
}

void test_attr (const char *dir)
{
    char path[PATH_MAX + 1];
    flux_conf_t *cf;
    json_t *hosts = NULL;
    struct boot_conf conf;
    attr_t *attrs;
    int rc;
    const char *input = \
"[bootstrap]\n" \
"curve_cert = \"foo\"\n" \
"hosts = [\n" \
"  { host = \"foo0\" },\n" \
"  { host = \"foo4\" },\n" \
"  { host = \"foo[1-5]\" },\n" \
"  { host = \"foo14\" },\n" \
"  { host = \"foo[6-9]\" },\n" \
"]\n";
    const char *val;
    int flags;

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok ((attrs = attr_create ()) != NULL,
        "attr_create_works");
    if (attrs == NULL)
        BAIL_OUT ("cannot continue without attrs");

    rc = boot_config_attr (attrs, "localhost", NULL);
    ok (rc == 0,
        "boot_config_attr works NULL hosts");
    ok (attr_get (attrs, "hostlist", NULL, NULL) == 0,
        "attr_get finds hostlist after NULL hosts");
    attr_destroy (attrs);

    attrs = attr_create ();
    if (!attrs)
        BAIL_OUT ("attr_create failed");
    hosts = json_array ();
    if (hosts == NULL)
        BAIL_OUT ("cannot continue without empty hosts array");
    rc = boot_config_attr (attrs, "localhost", hosts);
    ok (rc == 0,
        "boot_config_attr works empty hosts");
    ok (attr_get (attrs, "hostlist", NULL, NULL) == 0,
        "attr_get finds hostlist after empty hosts");
    json_decref (hosts);
    hosts = NULL;
    attr_destroy (attrs);

    rc = boot_config_parse (cf, &conf, &hosts);
    ok (rc == 0,
        "boot_conf_parse worked");
    if (hosts == NULL)
        BAIL_OUT ("cannot continue without hosts array");

    attrs = attr_create ();
    if (!attrs)
        BAIL_OUT ("attr_create failed");
    rc = boot_config_attr (attrs, "foo0", hosts);
    ok (rc == 0,
        "boot_config_attr works on input hosts");
    ok (attr_get (attrs, "hostlist", &val, &flags) == 0
        && streq (val, "foo[0,4,1-3,5,14,6-9]")
        && flags == ATTR_IMMUTABLE,
        "attr_get returns correct value and flags");

    json_decref (hosts);

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);
    flux_conf_decref (cf);
    attr_destroy (attrs);
}

void test_curve_cert (const char *dir)
{
    char path[PATH_MAX + 1];
    json_t *hosts;
    flux_conf_t *cf;
    struct boot_conf conf;
    const char *input = \
"[bootstrap]\n" \
"curve_cert = \"meep\"\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) == 0 && hosts == NULL,
        "boot_config_parse works with curve_cert");
    ok (conf.curve_cert != NULL && streq (conf.curve_cert, "meep"),
        "and curve_cert has expected value");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_ipv6 (const char *dir)
{
    char path[PATH_MAX + 1];
    json_t *hosts;
    flux_conf_t *cf;
    struct boot_conf conf;
    const char *input = \
"[bootstrap]\n" \
"enable_ipv6 = true\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) == 0 && hosts == NULL,
        "boot_config_parse works with enable_ipv6");
    ok (conf.enable_ipv6 != 0,
        "and enable_ipv6 has expected value");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    flux_conf_decref (cf);
}

void test_dup_hosts (const char *dir)
{
    char path[PATH_MAX + 1];
    json_t *hosts = NULL;
    const char *host;
    flux_conf_t *cf;
    struct boot_conf conf;
    const char *input = \
"[bootstrap]\n" \
"curve_cert = \"foo\"\n" \
"[[bootstrap.hosts]]\n" \
"host = \"test[0-127]\"\n" \
"[[bootstrap.hosts]]\n" \
"host = \"test[1,64]\"\n" \
"parent = \"test0\"\n" \
"[[bootstrap.hosts]]\n" \
"host = \"test[2-63]\"\n" \
"parent = \"test1\"\n" \
"[[bootstrap.hosts]]\n" \
"host = \"test[65-127]\"\n" \
"parent = \"test64\"\n";

    create_test_file (dir, "boot", "toml", path, sizeof (path), input);
    if (!(cf = flux_conf_parse (dir, NULL)))
        BAIL_OUT ("flux_conf_parse failed");

    ok (boot_config_parse (cf, &conf, &hosts) == 0,
        "boot_config_parse handles duplicate hosts");
    ok (hosts != NULL && json_array_size (hosts) == 128,
        "and post-processed hosts array has expected size");
    ok (json_unpack (json_array_get (hosts, 0), "{s:s}", "host", &host) == 0
        && streq (host, "test0"),
        "test0 has rank 0");
    ok (json_unpack (json_array_get (hosts, 65), "{s:s}", "host", &host) == 0
        && streq (host, "test65"),
        "test65 has rank 65");
    ok (json_unpack (json_array_get (hosts, 127), "{s:s}", "host", &host) == 0
        && streq (host, "test127"),
        "test127 has rank 127");

    if (unlink (path) < 0)
        BAIL_OUT ("could not cleanup test file %s", path);

    json_decref (hosts);
    flux_conf_decref (cf);
}

int main (int argc, char **argv)
{
    char dir[PATH_MAX + 1];

    plan (NO_PLAN);

    test_format ();

    create_test_dir (dir, sizeof (dir));

    test_parse (dir);
    test_overflow_bind (dir);
    test_overflow_connect (dir);
    test_bad_hosts_entry (dir);
    test_bad_host_hostlist (dir);
    test_bad_host_bind (dir);
    test_bad_host_key (dir);
    test_empty (dir);
    test_empty_hosts (dir);
    test_missing_info (dir);
    test_toml_mixed_array (dir);
    test_attr (dir);
    test_curve_cert (dir);
    test_ipv6 (dir);
    test_dup_hosts (dir);

    if (rmdir (dir) < 0)
        BAIL_OUT ("could not cleanup test dir %s", dir);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
