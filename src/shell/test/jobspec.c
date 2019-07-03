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
#include <jansson.h>

#include "jobspec.h"

#include "src/common/libtap/tap.h"

struct input {
    const char *desc;
    const char *s;
};

struct input good_input[] = {
    {
        "flux jobspec srun hostname",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    { NULL, NULL },
};
struct input bad_input[] = {
    {
        "empty object",
        "{}",
    },
    {
        "wrong version",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 256, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "missing version",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "missing resources",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1}",
    },
    {
        "missing tasks",
        "{\"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 256, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "environment not an object",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"environment\":42, \"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "cwd not a string",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": 42}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "no slot resource",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"meep\", \"label\": \"task\"}]}",
    },
    {
        "per_slot > 1",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 2}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "missing command",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    { NULL, NULL },
};

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    struct jobspec *js;
    json_error_t error;
    int i;

    for (i = 0; good_input[i].desc; i++) {
        js = jobspec_parse (good_input[i].s, &error);
        ok (js != NULL, "good.%d (%s) works", i, good_input[i].desc);
        if (!js)
            diag ("%s", error.text);
        else
            jobspec_destroy (js);
    }

    for (i = 0; bad_input[i].desc; i++) {
        js = jobspec_parse (bad_input[i].s, &error);
        ok (js == NULL, "bad.%d (%s) fails", i, bad_input[i].desc);
        if (!js)
            diag ("%s", error.text);
        else
            jobspec_destroy (js);
    }

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
