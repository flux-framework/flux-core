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

#include "src/common/libtap/tap.h"
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

    ok (upmi_initialize (upmi, &info, &error) == 0,
        "upmi_initialize works");
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
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    test_single ();
    test_inval ();

    done_testing ();
    return 0;
}

// vi:ts=4 sw=4 expandtab
