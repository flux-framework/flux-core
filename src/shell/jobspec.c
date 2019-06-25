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
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/log.h"

#include "jobspec.h"

struct res_level {
    const char *type;
    int count;
    json_t *with;
};

static void set_error (json_error_t *error, const char *fmt, ...)
{
    va_list ap;

    if (error) {
        va_start (ap, fmt);
        vsnprintf (error->text, sizeof (error->text), fmt, ap);
        va_end (ap);
    }
}

static int parse_res_level (json_t *o,
                            int level,
                            struct res_level *resp,
                            json_error_t *error)
{
    json_error_t loc_error;
    struct res_level res;

    if (o == NULL) {
        set_error (error, "level %d: missing", level);
        return -1;
    }
    res.with = NULL;
    /* For jobspec version 1, expect exactly one array element per level.
     */
    if (json_unpack_ex (o, &loc_error, 0,
                        "[{s:s s:i s?o}]",
                        "type", &res.type,
                        "count", &res.count,
                        "with", &res.with) < 0) {
        set_error (error, "level %d: %s", level, loc_error.text);
        return -1;
    }
    *resp = res;
    return 0;
}

void jobspec_destroy (struct jobspec *job)
{
    if (job) {
        json_decref (job->jobspec);
        free (job);
    }
}

struct jobspec *jobspec_parse (const char *jobspec, json_error_t *error)
{
    struct jobspec *job;
    int version;
    json_t *tasks;
    json_t *resources;
    struct res_level res[3];

    if (!(job = calloc (1, sizeof (*job)))) {
        set_error (error, "Out of memory");
        goto error;
    }
    if (!(job->jobspec = json_loads (jobspec, 0, error)))
        goto error;
    if (json_unpack_ex (job->jobspec, error, 0,
                        "{s:i s:o s:o s:{s:{s?:s s?:o}}}",
                        "version", &version,
                        "resources", &resources,
                        "tasks", &tasks,
                        "attributes",
                            "system",
                                "cwd", &job->cwd,
                                "environment", &job->environment) < 0) {
        goto error;
    }
    if (version != 1) {
        set_error (error, "Invalid jobspec version: expected 1 got %d",
                   version);
        goto error;
    }
    if (job->environment && !json_is_object (job->environment)) {
        set_error (error, "attributes.system.environment is not object type");
        goto error;
    }
    /* For jobspec version 1, expect either:
     * - node->slot->core->NIL
     * - slot->core->NIL
     * Set job->slot_count and job->cores_per_slot.
     */
    memset (res, 0, sizeof (res));
    if (parse_res_level (resources, 0, &res[0], error) < 0)
        goto error;
    if (res[0].with && parse_res_level (res[0].with, 1, &res[1], error) < 0)
        goto error;
    if (res[1].with && parse_res_level (res[1].with, 2, &res[2], error) < 0)
        goto error;
    if (res[0].type != NULL && !strcmp (res[0].type, "slot")
            && res[1].type != NULL && !strcmp (res[1].type, "core")
            && res[1].with == NULL) {
        job->slot_count = res[0].count;
        job->cores_per_slot = res[1].count;
        job->slots_per_node = -1; // unspecified
    }
    else if (res[0].type != NULL && !strcmp (res[0].type, "node")
            && res[1].type != NULL && !strcmp (res[1].type, "slot")
            && res[2].type != NULL && !strcmp (res[2].type, "core")
            && res[2].with == NULL) {
        job->slot_count = res[0].count * res[1].count;
        job->cores_per_slot = res[2].count;
        job->slots_per_node = res[1].count;
    }
    else {
        set_error (error, "Unexpected resource hierarchy: %s->%s->%s%s",
                   res[0].type ? res[0].type : "NULL",
                   res[1].type ? res[1].type : "NULL",
                   res[2].type ? res[2].type : "NULL",
                   res[2].with ? "->..." : NULL);
        goto error;
    }
    /* Set job->task_count
     */
    if (json_unpack_ex (tasks, NULL, 0,
                        "[{s:{s:i}}]",
                        "count", "total", &job->task_count) < 0) {
        int per_slot;
        if (json_unpack_ex (tasks, NULL, 0,
                            "[{s:{s:i}}]",
                            "count", "per_slot", &per_slot) < 0) {
            set_error (error, "Unable to parse task count");
            goto error;
        }
        if (per_slot != 1) {
            set_error (error, "per_slot count: expected 1 got %d", per_slot);
            goto error;
        }
        job->task_count = job->slot_count;
    }
    /* Get command
     */
    if (json_unpack_ex (tasks, NULL, 0,
                        "[{s:o}]",
                        "command", &job->command) < 0) {
        set_error (error, "Unable to parse command");
        goto error;
    }
    return job;
error:
    jobspec_destroy (job);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
