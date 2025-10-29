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
#include "rcalc.h"

static void set_error (json_error_t *error, const char *fmt, ...)
{
    va_list ap;

    if (error) {
        va_start (ap, fmt);
        vsnprintf (error->text, sizeof (error->text), fmt, ap);
        va_end (ap);
    }
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

/* This function requires that the jobspec resource ordering is the same as the
 * ordering specified in V1, but it allows additional resources before and in
 * between the V1 resources (i.e., node, slot, and core), with the restriction
 * that any additional resources between node and slot (if node present) must
 * have an integer count of 1. In shorthand, it requires that the jobspec
 * follows the form ...->[node]->...[1]->slot->...->core, Where `node` is
 * optional, and `...` represents any non-V1 resources, with [1] indicating a
 * count of 1 if `node` is present. Additionally, this function also allows
 * multiple resources at any level, as long as there is only a single node and
 * slot within the entire jobspec.
 */
static int recursive_get_slot_count (int *slot_count,
                                     json_t *curr_resource,
                                     json_error_t *error,
                                     bool *is_node_specified,
                                     int level)
{
    size_t index;
    json_t *value;
    const char *type;
    json_t *count;
    json_t *with;

    if (level == 0 ) {
        *slot_count = -1;
        *is_node_specified = false;
    }
    if (json_array_size (curr_resource) == 0) {
        set_error (error,
                   "level %d: Malformed jobspec: resource entry missing or not a list",
                   level);
        return -1;
    }
    json_array_foreach (curr_resource, index, value) {
        with = NULL;
        if (json_unpack_ex (value,
                            error,
                            0,
                            "{s:s s:o s?o}",
                            "type", &type,
                            "count", &count,
                            "with", &with) < 0) {
            set_error (error, "level %d: %s", level, error->text);
            return -1;
        }
        if (streq (type, "slot")) {
            if (*slot_count > 0) {
                set_error (error, "slot resource encountered after slot resource");
                return (*slot_count = -1);
            }
            if (!json_is_integer (count)) {
                set_error (error, "count must be integer for slot resource");
                return -1;
            }
            return (*slot_count = json_integer_value (count));
        }
        if (streq (type, "node")) {
            if (*is_node_specified) {
                set_error (error, "node resource encountered after node resource");
                return (*slot_count = -1);
            }
            *is_node_specified = true;
        }
        if (with) {
            recursive_get_slot_count (slot_count,
                                      with,
                                      error,
                                      is_node_specified,
                                      level+1);
        }
    }
    if (level == 0 && *slot_count < 1) {
        set_error (error, "Missing slot resource");
    }
    return (*slot_count);
}

struct jobspec *jobspec_parse (const char *jobspec,
                               rcalc_t *r,
                               json_error_t *error)
{
    struct jobspec *job;
    json_t *resources;
    json_t *count;
    bool is_node_specified;
    const char *type;

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
    if (json_unpack_ex (job->jobspec,
                        error,
                        0,
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

    if (r != NULL && rcalc_total_slots (r) > 0) {
        job->slot_count = rcalc_total_slots (r);
        job->cores_per_slot = rcalc_total_cores (r) / job->slot_count;
        /* Check whether nodes were explicitly specified in jobspec
         */
        if (json_unpack_ex (resources, error, 0, "[{s:s}]", "type", &type) < 0) {
            goto error;
        }
        if (streq (type, "node")) {
            job->node_count = rcalc_total_nodes (r);
            job->slots_per_node = job->slot_count / job->node_count;
        }
        else {
            job->node_count = -1;
            job->slots_per_node = -1;
        }
    } else {
        if (recursive_get_slot_count (&job->slot_count,
                                      resources,
                                      error,
                                      &is_node_specified,
                                      0) < 0) {
            // recursive_get_slot_count calls set_error
            goto error;
        }
        if (is_node_specified) {
            job->node_count = rcalc_total_nodes (r);
            job->slots_per_node = job->slot_count;
            job->slot_count *= job->node_count;
        } else {
            job->node_count = -1;
            job->slots_per_node = -1;
        }
        job->cores_per_slot = rcalc_total_cores (r) / job->slot_count;
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
