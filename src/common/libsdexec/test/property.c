/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "property.h"

void test_dict (void)
{
    json_t *dict;
    int val;

    if (!(dict = json_pack ("{s:[si]}", "foo", "i", 42)))
        BAIL_OUT ("could not create property dict for testing");

    ok (sdexec_property_dict_unpack (dict, "foo", "i", &val) == 0
        && val == 42,
        "sdexec_property_dict_unpack works");
    errno = 0;
    ok (sdexec_property_dict_unpack (dict, "unknown", "i", &val) < 0
        && errno == EPROTO,
        "sdexec_property_dict_unpack name=unknown fails with EPROTO");

    json_decref (dict);
}

void test_inval (void)
{
    flux_t *h;
    flux_future_t *f;
    json_t *dict;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(f = flux_future_create (NULL, 0)))
        BAIL_OUT ("could not create future for testing");
    if (!(dict = json_pack ("{s:[si]}", "foo", "i", 42)))
        BAIL_OUT ("could not create property dict for testing");

    errno = 0;
    ok (sdexec_property_get (NULL, "sdexec", 0, "foo", "bar") == NULL
        && errno == EINVAL,
        "sdexec_property_get h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get (h, NULL, 0, "foo", "bar") == NULL
        && errno == EINVAL,
        "sdexec_property_get service=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get (h, "sdexec", 0, NULL, "bar") == NULL
        && errno == EINVAL,
        "sdexec_property_get path=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get (h, "sdexec", 0, "foo", NULL) == NULL
        && errno == EINVAL,
        "sdexec_property_get name=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_get_unpack (NULL, "foo") < 0 && errno == EINVAL,
        "sdexec_property_get_unpack f=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get_unpack (f, NULL) < 0 && errno == EINVAL,
        "sdexec_property_get_unpack fmt=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_get_all (NULL, "sdexec", 0, "foo") == NULL
        && errno == EINVAL,
        "sdexec_property_get_all h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get_all (h, NULL, 0, "foo") == NULL
        && errno == EINVAL,
        "sdexec_property_get_all service=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_get_all (h, "sdexec", 0, NULL) == NULL
        && errno == EINVAL,
        "sdexec_property_get_all path=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_get_all_dict (NULL) == NULL && errno == EINVAL,
        "sdexec_property_get_all_dict f=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_changed (NULL, "sdexec", 0, "foo") == NULL
        && errno == EINVAL,
        "sdexec_property_changed h=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_changed_dict (NULL) == NULL && errno == EINVAL,
        "sdexec_property_changed_dict f=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_changed_path (NULL) == NULL && errno == EINVAL,
        "sdexec_property_changed_path f=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_property_dict_unpack (NULL, "foo", "bar") < 0
        && errno == EINVAL,
        "sdexec_property_dict_unpack dict=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_property_dict_unpack (dict, NULL, "bar") < 0
        && errno == EINVAL,
        "sdexec_property_dict_unpack name=NULL fails with EINVAL");
    ok (sdexec_property_dict_unpack (dict, "foo", NULL) < 0
        && errno == EINVAL,
        "sdexec_property_dict_unpack fmt=NULL fails with EINVAL");

    json_decref (dict);
    flux_future_destroy (f);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_dict ();
    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
