/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "src/common/libpmi/upmi.h"

void diag_trace (void *arg, const char *text)
{
    diag ("%s", text);
}

void test_single (void)
{
    struct upmi *upmi;
    flux_error_t error;
    struct upmi_info info;
    const char *name;
    char *val;

    upmi = upmi_create ("single",
                        UPMI_TRACE,
                        diag_trace,
                        NULL,
                        &error);
    ok (upmi != NULL,
        "upmi_create spec=single works");

    info.dict = json_true ();
    ok (upmi_initialize (upmi, &info, &error) == 0,
        "upmi_initialize works");
    ok (info.dict == NULL,
        "upmi_initialize sets info.dict to NULL by default");
    ok (info.size == 1 && info.rank == 0,
        "info rank==0, size==1");
    is (info.name, "single",
        "info name==single"); // normally jobid but not with spec=single
    name = upmi_describe (upmi);
    is (name, "single",
        "upmi_describe returns single");

    ok (upmi_put (upmi, "foo", "bar", &error) == 0,
        "upmi_put key=foo val=bar works");

    ok (upmi_barrier (upmi, &error) == 0,
        "upmi_barrier works");

    ok (upmi_get (upmi, "foo", -1, &val, &error) == 0,
        "upmi_get key=foo works");
    is (val, "bar",
        "value==bar");
    free (val);

    error.text[0] = '\0';
    ok (upmi_get (upmi, "notakey", -1, &val, &error) < 0,
        "upmi_get key=notakey fails");
    ok (strlen (error.text) > 0,
        "error.text was set");

    ok (upmi_finalize (upmi, &error) == 0,
        "upmi_finalize works");

    upmi_destroy (upmi);
}

void test_inval (void)
{
    struct upmi *upmi;
    flux_error_t error;

    error.text[0] = '\0';
    upmi = upmi_create ("notpmi", 0, NULL, NULL, &error);
    ok (upmi == NULL,
        "upmi_create spec=notpmi fails");
    diag ("%s", error.text);

    error.text[0] = '\0';
    upmi = upmi_create ("single", 0xffff, NULL, NULL, &error);
    ok (upmi == NULL,
        "upmi_create spec=single flags=0xffff fails");
    diag ("%s", error.text);

    upmi = upmi_create ("single", UPMI_TRACE, diag_trace, NULL, &error);
    if (!upmi)
        BAIL_OUT ("upmi_create spec=single failed");
    ok (upmi_initialize (NULL, NULL, &error) < 0,
        "upmi_initialize upmi=NULL fails");
    if (upmi_initialize (upmi, NULL, &error) < 0)
        BAIL_OUT ("upmi_initialize failed");
    upmi_destroy (upmi);

    error.text[0] = '\0';
    upmi = upmi_create_ex ("single", 0, json_null (), NULL, NULL, &error);
    ok (upmi == NULL,
        "upmi_create_ex args=json_null fails");
    diag ("%s", error.text);
}

/* The "singlex" dso (a clone of "single" with a different name) is
 * built as a dso in UPMI_TEST_SEARCHPATH
 */
void test_dso (void)
{
    if (setenv ("FLUX_PMI_CLIENT_SEARCHPATH", UPMI_TEST_SEARCHPATH, true) < 0)
        BAIL_OUT ("setenv failed");

    struct upmi *upmi;
    flux_error_t error;

    upmi = upmi_create ("singlex",
                        UPMI_TRACE,
                        diag_trace,
                        NULL,
                        &error);
    if (!upmi)
        diag ("%s", error.text);
    ok (upmi != NULL,
        "upmi_create spec=singlex works");

    upmi_destroy (upmi);

    (void)unsetenv ("FLUX_PMI_CLIENT_SEARCHPATH");
}

/* Ensure the environment can alter the methods search path
 */
void test_env (void)
{
    if (setenv ("FLUX_PMI_CLIENT_METHODS", "unknown", true) < 0)
        BAIL_OUT ("setenv failed");

    struct upmi *upmi;
    flux_error_t error;

    upmi = upmi_create (NULL,
                        UPMI_TRACE,
                        diag_trace,
                        NULL,
                        &error);
    if (!upmi)
        diag ("%s", error.text);
    ok (upmi == NULL,
        "upmi_create tries only FLUX_PMI_CLIENT_METHODS");

    if (setenv ("FLUX_PMI_CLIENT_SEARCHPATH", UPMI_TEST_SEARCHPATH, true) < 0)
        BAIL_OUT ("setenv failed");
    if (setenv ("FLUX_PMI_CLIENT_METHODS", "unknown singlex simple", true) < 0)
        BAIL_OUT ("setenv failed");

    upmi = upmi_create (NULL,
                        UPMI_TRACE,
                        diag_trace,
                        NULL,
                        &error);
    if (!upmi)
        diag ("%s", error.text);
    ok (upmi != NULL && streq (upmi_describe (upmi), "singlex"),
        "upmi_create respects FLUX_PMI_CLIENT_METHODS order");
    upmi_destroy (upmi);

    (void)unsetenv ("FLUX_PMI_CLIENT_METHODS");
    (void)unsetenv ("FLUX_PMI_CLIENT_SEARCHPATH");
}

void test_config (void)
{
    struct upmi *upmi;
    flux_error_t error;
    json_t *args;
    json_t *config;
    struct upmi_info info;
    char *value;

    args = json_pack ("{s:b}", "noflux", 1);
    if (args == NULL)
        BAIL_OUT ("could not create test args");
    upmi = upmi_create_ex ("config",
                           UPMI_TRACE | UPMI_LIBPMI_NOFLUX,
                           args,
                           diag_trace,
                           NULL,
                           &error);
    ok (upmi == NULL,
        "upmi_create spec=config fails with 'noflux' name collision")
        && diag ("%s", error.text);
    json_decref (args);

    upmi = upmi_create_ex ("config",
                           UPMI_TRACE,
                           NULL,
                           diag_trace,
                           NULL,
                           &error);
    ok (upmi == NULL,
        "upmi_create spec=config fails without args")
        && diag ("%s", error.text);

    config = json_pack ("{s:{s:i s:[{s:s s:s} {s:s}] s:s}}",
                        "bootstrap",
                          "default_port", 8050,
                          "hosts",
                            "host", "big0",
                              "connect", "tcp://big0:%p",
                            "host", "big[1-16383]",
                          "curve_cert", "/a/valid/path");
    if (config == NULL)
        BAIL_OUT ("could not create test config");

    args = json_pack ("{s:s s:O}",
                      "hostname", "big42",
                      "config", config);
    if (args == NULL)
        BAIL_OUT ("could not create test args");
    upmi = upmi_create_ex ("config",
                           UPMI_TRACE,
                           args,
                           diag_trace,
                           NULL,
                           &error);
    ok (upmi != NULL,
        "upmi_create_ex spec=config works with fake config")
        || diag ("%s", error.text);
    ok (upmi_initialize (upmi, &info, &error) == 0,
        "upmi_initialize works")
        || diag ("%s", error.text);
    cmp_ok (info.rank, "==", 42,
            "my rank is 42");
    cmp_ok (info.size, "==", 16384,
            "my size is 16384");

    value = NULL;
    ok (upmi_get (upmi, "42", -1, &value, &error) == 0
        && value != NULL,
        "upmi_get 42 fetched my business card")
        || diag ("%s", error.text);
    free (value);

    value = NULL;
    ok (upmi_get (upmi, "0", -1, &value, &error) == 0
        && value != NULL,
        "upmi_get 0 fetched leader's business card")
        || diag ("%s", error.text);
    free (value);

    upmi_destroy (upmi);

    json_decref (args);
    json_decref (config);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    (void)unsetenv ("FLUX_PMI_CLIENT_SEARCHPATH");
    (void)unsetenv ("FLUX_PMI_CLIENT_METHODS");

    test_single ();
    test_inval ();
    test_dso ();
    test_env ();
    test_config ();

    done_testing ();
    return 0;
}

// vi:ts=4 sw=4 expandtab
