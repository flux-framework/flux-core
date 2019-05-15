/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "rset.h"

struct resource_set_test {
    const char *descr;
    const char *input;
    const char *expected_ranks;
    double starttime;
    double expiration;
    const char *error_string;
};

#define RESOURCE_SET_TEST_END          \
    {                                  \
        NULL, NULL, NULL, 0., 0., NULL \
    }

#define BASIC_R                                      \
    "{ \"version\": 1,"                              \
    "  \"execution\": { "                            \
    "    \"starttime\":  12345, "                    \
    "    \"expiration\": 12445, "                    \
    "    \"R_lite\": "                               \
    "       [ {\"rank\": \"0-2\", "                  \
    "          \"children\": { \"core\": \"0-3\" } " \
    "         } "                                    \
    "       ] "                                      \
    "    } "                                         \
    "}"

#define BAD_VERSION                                  \
    "{ \"version\": 2,"                              \
    "  \"execution\": { "                            \
    "    \"starttime\":  12345, "                    \
    "    \"expiration\": 12445, "                    \
    "    \"R_lite\": "                               \
    "       [ {\"rank\": \"0-2\", "                  \
    "          \"children\": { \"core\": \"0-3\" } " \
    "         } "                                    \
    "       ] "                                      \
    "    } "                                         \
    "}"

#define BAD_IDSET                                    \
    "{ \"version\": 1,"                              \
    "  \"execution\": { "                            \
    "    \"starttime\":  12345, "                    \
    "    \"expiration\": 12445, "                    \
    "    \"R_lite\": "                               \
    "       [ {\"rank\": \"-2\", "                   \
    "          \"children\": { \"core\": \"0-3\" } " \
    "         } "                                    \
    "       ] "                                      \
    "    } "                                         \
    "}"

struct resource_set_test tests[] = {

    {
        "no R_lite",
        "{\"version\":1,\"execution\":{\"starttime\":0,\"expiration\":0}}",
        NULL,
        0.,
        0.,
        "Object item not found: R_lite",
    },
    {
        "invalid version",
        BAD_VERSION,
        NULL,
        0.,
        0.,
        "invalid version: 2",
    },
    {
        "invalid R_lite idset",
        BAD_IDSET,
        NULL,
        0.,
        0.,
        "R_lite: failed to read target rank list",
    },
    {"basic R check", BASIC_R, "0-2", 12345., 12445., NULL},
    RESOURCE_SET_TEST_END};

int main (int ac, char *av[])
{
    struct resource_set_test *e = NULL;

    plan (NO_PLAN);

    e = &tests[0];
    while (e && e->descr) {
        json_error_t err;
        struct resource_set *r = resource_set_create (e->input, &err);
        if (e->expected_ranks == NULL) {  // Expected failure
            ok (r == NULL, "%s: resource_set_create expected failure", e->descr);
            is (err.text, e->error_string, "%s: got expected error text", e->descr);
        } else {
            if (r) {
                const struct idset *ids = resource_set_ranks (r);
                double starttime = resource_set_starttime (r);
                double expiration = resource_set_expiration (r);
                if (ids == NULL)
                    fail ("%s: resource_set_ranks() failed", e->descr);
                else {
                    char *ranks = idset_encode (ids, IDSET_FLAG_RANGE);
                    is (ranks,
                        e->expected_ranks,
                        "%s: expect target ranks (%s)",
                        e->descr,
                        ranks);
                    free (ranks);
                }
                ok (starttime == e->starttime,
                    "%s: expect starttime %.2f (got %.2f)",
                    e->descr,
                    e->starttime,
                    starttime);
                ok (expiration == e->expiration,
                    "%s: expect expiration %.2f (got %.2f)",
                    e->descr,
                    e->expiration,
                    expiration);
            } else {
                fail ("%s: %d:[%s]", e->descr, err.position, err.text);
            }
        }
        resource_set_destroy (r);
        e++;
    }

    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
