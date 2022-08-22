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
#include <jansson.h>
#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"

#include "util.h"

void test_join (void)
{
    json_t *o;
    char *s;

    if (!(o = json_pack ("[s]", "foo")))
        BAIL_OUT ("could not create json array");
    s = util_join_arguments (o);
    ok (s && !strcmp (s, "foo"),
        "util_join_arguments [foo] works");
    free (s);
    json_decref (o);

    if (!(o = json_pack ("[sss]", "foo", "bar", "baz")))
        BAIL_OUT ("could not create json array");
    s = util_join_arguments (o);
    ok (s && !strcmp (s, "foo,bar,baz"),
        "util_join_arguments [foo,bar,baz] works");
    free (s);
    json_decref (o);

    if (!(o = json_array ()))
        BAIL_OUT ("could not create json array");
    s = util_join_arguments (o);
    ok (s && !strcmp (s, ""),
        "util_join_arguments [] works");
    free (s);
    json_decref (o);

    if (!(o = json_object ()))
        BAIL_OUT ("could not create json object");
    errno = 0;
    s = util_join_arguments (o);
    ok (s == NULL && errno == EINVAL,
        "util_join_arguments object fails with EINVAL");
    json_decref (o);

    if (!(o = json_pack ("[i]", 42)))
        BAIL_OUT ("could not create json array");
    errno = 0;
    s = util_join_arguments (o);
    ok (s == NULL && errno == EINVAL,
        "util_join_arguments [42] fails with EINVAL");
    json_decref (o);

    errno = 0;
    s = util_join_arguments (NULL);
    ok (s == NULL && errno == EINVAL,
        "util_join_arguments NULL fails with EINVAL");
}


int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_join ();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
