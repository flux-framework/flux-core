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
#include "ccan/str/str.h"

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
                        "{s:s s:i s?o}",
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
        /*  refcounts were incremented on environment, options */
        json_decref (job->environment);
        json_decref (job->options);
        json_decref (job->jobspec);
        free (job);
    }
}

static int recursive_parse_helper (struct jobspec *job,
                                  json_t *curr_resource,
                                  json_error_t *error,
                                  int level,
                                  int with_multiplier)
{
    size_t index;
    json_t *value;
    size_t size = json_array_size (curr_resource);
    struct res_level res;
    int curr_multiplier;

    if (size == 0) {
        set_error (error, "Malformed jobspec: resource entry is not a list");
        return -1;
    }

    json_array_foreach (curr_resource, index, value) {
        if (parse_res_level (value, level, &res, error) < 0) {
            return -1;
        }

        curr_multiplier = with_multiplier * res.count;

        if (streq (res.type, "node")) {
            if (job->slot_count > 0) {
                set_error (error, "node resource encountered after slot resource");
                return -1;
            }
            if (job->cores_per_slot > 0) {
                set_error (error, "node resource encountered after core resource");
                return -1;
            }
            if (job->node_count > 0) {
                set_error (error, "node resource encountered after node resource");
                return -1;
            }

            job->node_count = curr_multiplier;
        } else if (streq (res.type, "slot")) {
            if (job->cores_per_slot > 0) {
                set_error (error, "slot resource encountered after core resource");
                return -1;
            }
            if (job->slot_count > 0) {
                set_error (error, "slot resource encountered after slot resource");
                return -1;
            }

            job->slot_count = curr_multiplier;

            // Reset the multiplier since we are now looking
            // to calculate the cores_per_slot value
            curr_multiplier = 1;

            // Check if we already encountered the `node` resource
            if (job->node_count > 0) {
                // N.B.: with a strictly enforced ordering of node then slot
                // (with arbitrary non-core resources in between)
                // the slots_per_node will always be a perfectly round integer
                // (i.e., job->slot_count % job->node_count == 0)
                job->slots_per_node = job->slot_count / job->node_count;
            }
        } else if (streq (res.type, "core")) {
            if (job->slot_count < 1) {
                set_error (error, "core resource encountered before slot resource");
                return -1;
            }
            if (job->cores_per_slot > 0) {
                set_error (error, "core resource encountered after core resource");
                return -1;
            }

            job->cores_per_slot = curr_multiplier;
            // N.B.: despite having found everything we were looking for (i.e.,
            // node, slot, and core resources), we have to keep recursing to
            // make sure their aren't additional erroneous node/slot/core
            // resources in the jobspec
        }

        if (res.with != NULL) {
            if (recursive_parse_helper (job,
                                        res.with,
                                        error,
                                        level+1,
                                        curr_multiplier)
                < 0) {
                return -1;
            }
        }

        if (streq (res.type, "node")) {
            if ((job->slot_count <= 0) || (job->cores_per_slot <= 0)) {
                set_error (error,
                           "node encountered without slot&core below it");
                return -1;
            }
        } else if (streq (res.type, "slot")) {
            if (job->cores_per_slot <= 0) {
                set_error (error, "slot encountered without core below it");
                return -1;
            }
        }
    }
    return 0;
}

/* This function requires that the jobspec resource ordering is the same as the
 * ordering specified in V1, but it allows additional resources before and in
 * between the V1 resources (i.e., node, slot, and core).  In shorthand, it
 * requires that the jobspec follows the form ...->[node]->...->slot->...->core.
 * Where `node` is optional, and `...` represents any non-V1
 * resources. Additionally, this function also allows multiple resources at any
 * level, as long as there is only a single node, slot, and core within the
 * entire jobspec.
 */
static int recursive_parse_jobspec_resources (struct jobspec *job,
                                              json_t *curr_resource,
                                              json_error_t *error)
{
    if (curr_resource == NULL) {
        set_error (error, "jobspec top-level resources empty");
        return -1;
    }

    // Set node-related values to -1 ahead of time, if the recursive descent
    // encounters node in the jobspec, it will overwrite these values
    job->slots_per_node = -1;
    job->node_count = -1;

    int rc = recursive_parse_helper (job, curr_resource, error, 0, 1);

    if ((rc == 0) && (job->cores_per_slot < 1)) {
        set_error (error, "Missing core resource");
        return -1;
    }
    return rc;
}

struct jobspec *jobspec_parse (const char *jobspec, json_error_t *error)
{
    struct jobspec *job;
    json_t *resources;
    json_t *count;

    if (!(job = calloc (1, sizeof (*job)))) {
        set_error (error, "Out of memory");
        goto error;
    }
    if (!(job->jobspec = json_loads (jobspec, 0, error)))
        goto error;

    /* N.B.: members of jobspec like environment and shell.options may
     *  be modified with json_object_update_new() via the shell API
     *  calls flux_shell_setenvf(3), flux_shell_unsetenv(3), and
     *  flux_shell_setopt(3). Therefore, the refcount of these objects
     *  is incremented during unpack (via the "O" specifier), so that
     *  the objects have json_decref() called directly on them to
     *  avoid potential leaks (the json_decref() of the outer jobspec
     *  object itself doesn't seem to catch the changes to these inner
     *  json_t * objects)
     */
    if (json_unpack_ex (job->jobspec, error, 0,
                        "{s:i s:o s:[{s:o s:o}] s:{s?{s?s s?O s?{s?O}}}}",
                        "version", &job->version,
                        "resources", &resources,
                        "tasks",
                            "command", &job->command,
                            "count", &count,
                        "attributes",
                            "system",
                                "cwd", &job->cwd,
                                "environment", &job->environment,
                                "shell", "options", &job->options) < 0) {
        goto error;
    }
    if (job->version != 1) {
        set_error (error, "Invalid jobspec version: expected 1 got %d",
                   job->version);
        goto error;
    }
    if (job->environment && !json_is_object (job->environment)) {
        set_error (error, "attributes.system.environment is not object type");
        goto error;
    }
    /* Ensure that shell options and environment are never NULL, so a shell
     * component or plugin may set a new option or environment var.
     */
    if ((!job->options && !(job->options = json_object ()))
        || (!job->environment && !(job->environment = json_object ()))) {
        set_error (error, "unable to create empty jobspec options/environment");
        goto error;
    }

    if (recursive_parse_jobspec_resources (job, resources, error) < 0) {
        // recursive_parse_jobspec_resources calls set_error
        goto error;
    }

    /* Set job->task_count
     */
    if (json_object_size (count) != 1) {
        set_error (error, "tasks count must have exactly one key set");
        goto error;
    }
    if (json_unpack (count, "{s:i}", "total", &job->task_count) < 0) {
        int per_slot;
        if (json_unpack (count, "{s:i}", "per_slot", &per_slot) < 0) {
            set_error (error, "Unable to parse tasks count");
            goto error;
        }
        if (per_slot != 1) {
            set_error (error, "per_slot count: expected 1 got %d", per_slot);
            goto error;
        }
        job->task_count = job->slot_count;
    }
    /* Check command
     */
    if (!json_is_array (job->command)) {
        set_error (error, "Malformed command entry");
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
