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
#include "rcalc.h"

#include "src/common/libtap/tap.h"

struct input {
    const char *desc;
    const char *j;
    const char *r;
};

struct output {
    int task_count;
    int slot_count;
    int cores_per_slot;
    int slots_per_node;
};

struct input good_input[] = {
    {
        "slot->core",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "slot->core (different version number)",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 256, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "node->socket->slot->core",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "slot[5]->socket[2]->core[3]",
        "{\"resources\": [{\"type\": \"slot\", \"count\": 5, \"label\": \"task\", \"with\": [{\"type\": \"socket\", \"count\": 2, \"with\": [{\"type\": \"core\", \"count\": 3}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1-30\"}}]}}",
    },
    {
        "node->socket->slot->(core[2],gpu)",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 2}, {\"type\": \"gpu\", \"count\": 1}]}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1-2\", \"gpu\": \"1\"}}]}}",
    },
    {
        "node->socket->slot->(gpu,core)",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"type\": \"gpu\", \"count\": 1}, {\"type\": \"core\", \"count\": 1}]}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\", \"gpu\": \"1\"}}]}}",
    },
    {
        "node->socket->slot->(core[2]->PU,gpu)",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 2, \"with\": [{\"type\": \"PU\", \"count\": 1}]}, {\"type\": \"gpu\", \"count\": 1}]}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1-2\", \"gpu\": \"1\"}}]}}",
    },
    {
        "node[2]->(storage,slot[3]->core[5])",
        "{\"resources\": [{\"type\": \"node\", \"count\": 2, \"with\": [{\"type\": \"storage\", \"count\": 1}, {\"type\": \"slot\", \"label\": \"task\", \"count\": 3, \"with\": [{\"type\": \"core\", \"count\": 5}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1-2\", \"children\": {\"core\": \"1-15\"}}]}}",
    },
    {
        "(storage,node->slot->core)",
        "{\"version\": 1, \"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"default\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}]}]}, {\"type\": \"storage\", \"count\": 1562, \"exclusive\": true}], \"attributes\": {\"system\": {\"duration\": 57600}}, \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"default\", \"count\": {\"per_slot\": 1}}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "cluster->(storage,node->slot->core)",
        "{\"version\": 1, \"resources\": [{\"type\": \"cluster\", \"count\": 1, \"with\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"default\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}]}]}, {\"type\": \"storage\", \"count\": 1562, \"exclusive\": true}]}], \"attributes\": {\"system\": {\"duration\": 57600}}, \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"default\", \"count\": {\"per_slot\": 1}}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    { NULL, NULL, NULL },
};
struct output good_output[] = {
     {1, 1, 1, -1},
     {1, 1, 1, -1},
     {1, 1, 1, 1},
     {5, 5, 6, -1},
     {1, 1, 2, 1},
     {1, 1, 1, 1},
     {1, 1, 2, 1},
     {6, 6, 5, 3},
     {1, 1, 1, 1},
     {1, 1, 1, 1},
     {0, 0, 0, 0},
};
struct input bad_input[] = {
    { "empty object", "{}",  "{}" },
    { "invalid JSON", "{]",  "{}" },
    {
        "missing version",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "missing resources",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1}",
        "{}",
    },
    {
        "missing tasks",
        "{\"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 256, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "environment not an object",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"environment\":42, \"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "cwd not a string",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": 42}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "no slot resource",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"meep\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "missing task count",
        "{\"tasks\": [{\"slot\": \"task\", \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "empty task count",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "multiple keys in task count",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1, \"total\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "per_resource",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_resource\": {\"type\": \"core\", \"count\": 1}}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "per_slot > 1",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 2}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "missing command",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": \"slot\", \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "slot count not an integer",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": {\"min\": 1}, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1-2\"}}]}}",
    },
    {
        "resources not an array",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": {\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "command not an array",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": \"hostname\", \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "missing resource type",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": \"hostname\", \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "invalid resource type",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}], \"type\": 1, \"label\": \"task\"}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}], \"nslots\": 1}}",
    },
    {
        "(node->slot->core,slot->core)",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}, {\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1-2\"}}]}}",
    },
    {
        "(node->slot->core,node)",
        "{\"tasks\": [{\"slot\": \"task\", \"count\": {\"per_slot\": 1}, \"command\": [\"hostname\"], \"attributes\": {}}], \"attributes\": {\"system\": {\"cwd\": \"/home/garlick/proj/flux-core/src/cmd\"}}, \"version\": 1, \"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"label\": \"task\", \"count\": 1, \"with\": [{\"count\": 1, \"type\": \"core\"}]}]}, {\"type\": \"node\", \"count\": 1}]}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1-2\", \"children\": {\"core\": \"1\"}}]}}",
    },
    { NULL, NULL, NULL },
};

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    struct jobspec *js;
    struct output *expect;
    rcalc_t *rs;
    json_error_t error;
    int i;

    for (i = 0; good_input[i].desc; i++) {
        rs = rcalc_create (good_input[i].r);
        js = jobspec_parse (good_input[i].j, rs, &error);
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
        rcalc_destroy (rs);
    }

    for (i = 0; bad_input[i].desc; i++) {
        rs = rcalc_create (bad_input[i].r);
        js = jobspec_parse (bad_input[i].j, rs, &error);
        ok (js == NULL, "bad.%d (%s) fails", i, bad_input[i].desc);
        if (!js)
            diag ("%s", error.text);
        else
            jobspec_destroy (js);
        rcalc_destroy (rs);
    }

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
