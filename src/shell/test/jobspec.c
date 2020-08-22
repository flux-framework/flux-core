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

struct output {
    int task_count;
    int slot_count;
    int cores_per_slot;
    int slots_per_node;
};

struct input good_input[] = {
    {
        "flux jobspec srun hostname",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
    },
    {
        "node->socket->slot->core",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    {
        "node[2]->socket[3]->slot[5]->core[3]",
        "{\"resources\": [{\"type\": \"node\", \"count\": 2, \"with\": [{\"type\": \"socket\", \"count\": 3, \"with\": [{\"type\": \"slot\", \"count\": 5, \"with\": [{\"type\": \"core\", \"count\": 3}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    {
        "slot[5]->socket[2]->core[3]",
        "{\"resources\": [{\"type\": \"slot\", \"count\": 5, \"label\": \"task\", \"with\": [{\"type\": \"socket\", \"count\": 2, \"with\": [{\"type\": \"core\", \"count\": 3}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    {
        "node->socket->slot->(core[2],gpu)",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 2}, {\"type\": \"gpu\", \"count\": 1}]}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    { NULL, NULL },
};
struct output good_output[] =
    {
     {1, 1, 1, -1},
     {1, 1, 1, 1},
     {30, 30, 3, 15},
     {5, 5, 6, -1},
     {1, 1, 2, 1},
     {0, 0, 0, 0},
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
    {
        "slot->node->core",
        "{\"resources\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    {
        "node->core->slot",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
    },
    { NULL, NULL },
};

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    struct jobspec *js;
    struct output *expect;
    json_error_t error;
    int i;

    for (i = 0; good_input[i].desc; i++) {
        js = jobspec_parse (good_input[i].s, &error);
        ok (js != NULL, "good.%d (%s) works", i, good_input[i].desc);
        if (!js) {
            diag ("%s", error.text);
        } else {
            expect = &good_output[i];
            ok (js->task_count == expect->task_count,
                "good.%d (%s) task count (%d) == %d",
                i,
                good_input[i].desc,
                js->task_count,
                expect->task_count);
            ok (js->slot_count == expect->slot_count,
                "good.%d (%s) slot count (%d) == %d",
                i,
                good_input[i].desc,
                js->slot_count,
                expect->slot_count);
            ok (js->cores_per_slot == expect->cores_per_slot,
                "good.%d (%s) cores per slot (%d) == %d",
                i,
                good_input[i].desc,
                js->cores_per_slot,
                expect->cores_per_slot);
            ok (js->slots_per_node == expect->slots_per_node,
                "good.%d (%s) slots per node (%d) == %d",
                i,
                good_input[i].desc,
                js->slots_per_node,
                expect->slots_per_node);
            jobspec_destroy (js);
        }
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
