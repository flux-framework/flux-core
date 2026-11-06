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
#include <errno.h>
#include <jansson.h>

#include "ccan/str/str.h"

#include "jobspec.h"
#include "slotmap.h"

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
    const char *per_resource;
    int per_resource_count;
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
    {
        "per-resource: type=node",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}, \"shell\": {\"options\": {\"per-resource\": {\"type\": \"node\"}}}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "per-resource: type=core",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}, \"shell\": {\"options\": {\"per-resource\": {\"type\": \"core\"}}}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "per-resource: type=core count=2",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}, \"shell\": {\"options\": {\"per-resource\": {\"type\": \"core\", \"count\": 2}}}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1\", \"children\": {\"core\": \"1\"}}]}}",
    },
    { NULL, NULL, NULL },
};
struct output good_output[] = {
     {1, 1, 1, -1, NULL, -1},
     {1, 1, 1, -1, NULL, -1},
     {1, 1, 1, 1, NULL, -1},
     {5, 5, 6, -1, NULL, -1},
     {1, 1, 2, 1, NULL, -1},
     {1, 1, 1, 1, NULL, -1},
     {1, 1, 2, 1, NULL, -1},
     {6, 6, 5, 3, NULL, -1},
     {1, 1, 1, 1, NULL, -1},
     {1, 1, 1, 1, NULL, -1},
     {1, 1, 1, 1, "node", 1},
     {1, 1, 1, 1, "core", 1},
     {1, 1, 1, 1, "core", 2},
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
    {
        "invalid shell.options.per-resource",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}, \"shell\": {\"options\": {\"per-resource\": {\"count\": 2}}}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1-2\", \"children\": {\"core\": \"1\"}}]}}",
    },
    {
        "invalid shell.options.per-resource",
        "{\"resources\": [{\"type\": \"node\", \"count\": 1, \"with\": [{\"type\": \"socket\", \"count\": 1, \"with\": [{\"type\": \"slot\", \"count\": 1, \"with\": [{\"type\": \"core\", \"count\": 1}], \"label\": \"task\"}]}]}], \"tasks\": [{\"command\": [\"hostname\"], \"slot\": \"task\", \"count\": {\"per_slot\": 1}}], \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/usr/libexec/flux\", \"environment\": {}, \"shell\": {\"options\": {\"per-resource\": {\"type\": \"node\", \"count\": 0}}}}}, \"version\": 1}",
        "{\"version\": 1, \"execution\": {\"R_lite\": [{\"rank\": \"1-2\", \"children\": {\"core\": \"1\"}}]}}",
    },
    { NULL, NULL, NULL },
};

static struct slotmap *slotmap_from_string (const char *s)
{
    json_t *R = json_loads (s, 0, NULL);
    struct slotmap *sm = slotmap_create (R, NULL);
    json_decref (R);
    return sm;
}

/* A jobspec with one task per slot label, as for the labels of RFC 14
 * "or" slots.
 */
static const char *multitask_jobspec =
    "{\"version\": 1,"
    " \"resources\": [{\"type\": \"slot\", \"count\": 1, \"label\": \"alpha\","
    "   \"with\": [{\"type\": \"core\", \"count\": 1}]}],"
    " \"tasks\": ["
    "   {\"command\": [\"app_a\"], \"slot\": \"alpha\","
    "    \"count\": {\"per_slot\": 1}},"
    "   {\"command\": [\"app_b\"], \"slot\": \"beta\","
    "    \"count\": {\"per_slot\": 1}}],"
    " \"attributes\": {\"system\": {\"duration\": 0, \"cwd\": \"/tmp\"}}}";

static const char *multitask_R =
    "{\"version\": 1, \"execution\": {\"R_lite\":"
    " [{\"rank\": \"0\", \"children\": {\"core\": \"0\"}}]}}";

static const char *command0 (json_t *command)
{
    return json_string_value (json_array_get (command, 0));
}

/* Return a copy of multitask_jobspec with tasks[1] replaced by `task`.
 */
static char *multitask_with (const char *task)
{
    json_t *o = json_loads (multitask_jobspec, 0, NULL);
    json_t *t = json_loads (task, 0, NULL);
    char *s;
    if (!o || !t || json_array_set_new (json_object_get (o, "tasks"), 1, t))
        BAIL_OUT ("failed to create jobspec");
    s = json_dumps (o, JSON_COMPACT);
    json_decref (o);
    return s;
}

static void test_multitask (void)
{
    struct slotmap *sm;
    struct jobspec *js;
    json_error_t error;
    struct {
        const char *desc;
        const char *task;
        const char *command;
    } other[] = {
        {
            "task without slot",
            "{\"command\": [\"b\"], \"count\": {\"per_slot\": 1}}",
            "app_a",
        },
        {
            "task with total count",
            "{\"command\": [\"b\"], \"slot\": \"beta\","
            " \"count\": {\"total\": 2}}",
            "b",
        },
        {
            "task with the same slot as the first task",
            "{\"command\": [\"b\"], \"slot\": \"alpha\","
            " \"count\": {\"per_slot\": 1}}",
            "app_a",
        },
        { NULL, NULL, NULL },
    };

    if (!(sm = slotmap_from_string (multitask_R)))
        BAIL_OUT ("slotmap_create failed");
    js = jobspec_parse (multitask_jobspec, sm, &error);
    ok (js != NULL, "multi-task jobspec parses");
    if (!js)
        diag ("%s", error.text);
    else {
        ok (js->labels != NULL
            && streq (js->labels[0], "alpha")
            && streq (js->labels[1], "beta")
            && js->labels[2] == NULL,
            "labels are alpha and beta");
        is (command0 (js->command), "app_a", "default command is tasks[0]");
        is (command0 (jobspec_task_command (js, NULL)),
            "app_a",
            "command for NULL label is tasks[0]");
        is (command0 (jobspec_task_command (js, "beta")),
            "app_b",
            "command for label beta is app_b");
        is (command0 (jobspec_task_command (js, "alpha")),
            "app_a",
            "command for label alpha is app_a");
        errno = 0;
        ok (jobspec_task_command (js, "gamma") == NULL && errno == ENOENT,
            "command for unknown label fails with ENOENT");
        jobspec_destroy (js);
    }

    /* Other tasks need not have a slot, a per_slot count, or a distinct
     * slot. The command for a label is that of the first task bound to it.
     */
    for (int i = 0; other[i].desc; i++) {
        char *s = multitask_with (other[i].task);
        js = jobspec_parse (s, sm, &error);
        ok (js != NULL, "multi-task jobspec with %s parses", other[i].desc);
        if (!js)
            diag ("%s", error.text);
        else {
            is (command0 (jobspec_task_command (js, "beta")
                          ? jobspec_task_command (js, "beta")
                          : jobspec_task_command (js, "alpha")),
                other[i].command,
                "command for label beta (or alpha) is %s",
                other[i].command);
        }
        jobspec_destroy (js);
        free (s);
    }

    /* The command of every task must be an array
     */
    {
        char *s = multitask_with ("{\"command\": \"b\", \"slot\": \"beta\","
                                  " \"count\": {\"per_slot\": 1}}");
        js = jobspec_parse (s, sm, &error);
        ok (js == NULL && streq (error.text, "Malformed command entry"),
            "multi-task jobspec with malformed command in tasks[1] fails");
        jobspec_destroy (js);
        free (s);
    }

    /* A single task jobspec has no labels if the task has no slot
     */
    js = jobspec_parse (good_input[0].j, sm, &error);
    ok (js != NULL && js->labels != NULL && streq (js->labels[0], "task"),
        "single task jobspec has its slot label");
    jobspec_destroy (js);
    slotmap_destroy (sm);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    struct jobspec *js;
    struct output *expect;
    struct slotmap *rs;
    json_error_t error;
    int i;

    for (i = 0; good_input[i].desc; i++) {
        rs = slotmap_from_string (good_input[i].r);
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
            if (expect->per_resource == NULL)
                ok (js->per_resource == NULL,
                    "good.%d (%s) per_resource == NULL",
                    i,
                    good_input[i].desc);
            else {
                is (js->per_resource,
                    expect->per_resource,
                    "good.%d (%s) per_resource (%s) == (%s)",
                    i,
                    good_input[i].desc,
                    js->per_resource,
                    expect->per_resource);
                ok (js->per_resource_count == expect->per_resource_count,
                    "good.%d (%s) per-resource count (%d) == %d",
                    i,
                    good_input[i].desc,
                    js->per_resource_count,
                    expect->per_resource_count);
            }
            jobspec_destroy (js);
        }
        slotmap_destroy (rs);
    }

    for (i = 0; bad_input[i].desc; i++) {
        rs = slotmap_from_string (bad_input[i].r);
        js = jobspec_parse (bad_input[i].j, rs, &error);
        ok (js == NULL, "bad.%d (%s) fails", i, bad_input[i].desc);
        if (!js)
            diag ("%s", error.text);
        else
            jobspec_destroy (js);
        slotmap_destroy (rs);
    }

    test_multitask ();

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
