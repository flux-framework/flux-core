/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "mustache.h"

struct mustache_test {
    const char *template;
    const char *expected;
    int errnum;
};

struct mustache_test tests[] = {
    { "", "", 0 },
    { "notemplate", "notemplate", 0 },
    { "{{", "{{", 0 },
    { "foo-{{", "foo-{{", 0 },
    { "}}", "}}", 0 },
    { "foo-}}", "foo-}}", 0 },
    { "{{boop}}", "{{boop}}", 0 },
    { "test-{{name}}", "test-foo", 0 },
    { "test-{{name}}.out", "test-foo.out", 0 },
    { "test-{{name}}.out", "test-foo.out", 0 },
    { "{{number}}", "42", 0 },
    { "{{name}}-{{number}}.out", "foo-42.out", 0 },
    { "{{name}}-{{number}}.out", "foo-42.out", 0 },
    { NULL, NULL, 0 },
};


int cb (FILE *fp, const char *tag, void *arg)
{
    ok (fp != NULL,
        "cb passed valid FILE *");
    ok (tag != NULL,
        "cb passed valid tag");
    ok (arg != NULL,
        "cb passed valid arg");
    if (streq (tag, "name"))
        return fputs ("foo", fp);
    if (streq (tag, "number"))
        return fputs ("42", fp);
    errno = ENOENT;
    return -1;
}

int main (int argc, char **argv)
{
    struct mustache_test *mp = tests;
    struct mustache_renderer *mr = NULL;

    plan (NO_PLAN);

    mr = mustache_renderer_create (NULL);
    ok (mr == NULL && errno == EINVAL,
        "mustache_renderer_create fails with invalid callback");

    /*  Pass tests as tag_f argument so we have something non-NULL to test
     *   in the callback
     */
    mr = mustache_renderer_create (cb);
    ok (mr != NULL,
        "mustache_renderer_create");

    ok (mustache_render (mr, NULL, tests) == NULL && errno == EINVAL,
        "mustache_render (mr, NULL) returns EINVAL");

    while (mp->template != NULL) {
        char * result = mustache_render (mr, mp->template, tests);
        if (mp->expected == NULL)
            ok (result == NULL && errno == mp->errnum,
                "mustache_render '%s' failed with errno = %d",
                mp->template,
                errno);
        else
            is (result, mp->expected,
                "mustache_render '%s' returned '%s'",
                mp->template,
                result);
        free (result);
        mp++;
    }

    mustache_renderer_destroy (mr);
    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
