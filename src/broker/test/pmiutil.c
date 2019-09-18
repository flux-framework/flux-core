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
#include <stdlib.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "src/common/libpmi/pmi.h"

#include "pmiutil.h"

int main (int argc, char **argv)
{
    struct pmi_handle *pmi;
    int result;
    struct pmi_params params;
    char val[64];

    plan (NO_PLAN);

    /* Enable some debug output on stderr.
     */
    (void)setenv ("PMI_DEBUG", "1", 1);

    /* Force singleton (ours) by ensuring pmi_simple_client
     * and dlopen will fail
     */
    (void)unsetenv ("PMI_FD");
    (void)unsetenv ("PMI_RANK");
    (void)unsetenv ("PMI_FD");
    (void)setenv ("PMI_LIBRARY", "/nope.so", 1);

    pmi = broker_pmi_create ();
    ok (pmi != NULL,
        "broker_pmi_create() works (singleton)");

    result = broker_pmi_init (pmi);
    ok (result == PMI_SUCCESS,
        "broker_pmi_init() works");

    memset (&params, 0, sizeof (params));
    result = broker_pmi_get_params (pmi, &params);
    ok (result == PMI_SUCCESS,
        "broker_pmi_get_params() works");
    ok (params.rank == 0 && params.size == 1,
        "rank=0 size=1");
    ok (strlen (params.kvsname) > 0,
        "kvsname is not the empty string");
    diag ("kvsname=%s", params.kvsname);

    result = broker_pmi_kvs_put (pmi, params.kvsname, "foo", "bar");
    ok (result == PMI_SUCCESS,
        "broker_pmi_kvs_put %s foo=bar works", params.kvsname);

    result = broker_pmi_barrier (pmi);
    ok (result == PMI_SUCCESS,
        "broker_pmi_barrier works");

    result = broker_pmi_kvs_get (pmi, params.kvsname, "foo", val, sizeof (val));
    ok (result != PMI_SUCCESS,
        "broker_pmi_kvs_get fails since singleton doesn't implement kvs");
    // at least while we can get away without it!

    result = broker_pmi_finalize (pmi);
    ok (result == PMI_SUCCESS,
        "broker_pmi_finalize() works");

    broker_pmi_destroy (pmi);
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
