/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobspec-update-job-list.c - test jobspec-update event in job-list
 * module
 */

#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"

static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:f}",
                                         "attributes.system.duration",
                                         1000.0) , 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "update failure");
        return -1;
    }
    return 0;
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    json_t *o = NULL;
    json_t *jobspec_tasks = NULL;
    json_t *task;
    json_t *command;
    json_t *new_command = NULL;
    int rc = -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s:o}}",
                                "jobspec",
                                  "tasks", &o) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "cannot read jobspec");
        goto cleanup;
    }

    if (!(jobspec_tasks = json_deep_copy (o))
        || !(task = json_array_get (jobspec_tasks, 0))
        || !(command = json_object_get (task, "command"))
        || !(new_command = json_string ("hostname"))
        || json_array_set (command, 0, new_command) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "cannot update jobspec tasks");
        goto cleanup;
    }

    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:O}",
                                         "tasks", jobspec_tasks) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "update failure");
        goto cleanup;
    }

    rc = 0;
cleanup:
    json_decref (jobspec_tasks);
    json_decref (new_command);
    return rc;
}

static int sched_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    static bool updated = false;
    /* Avoid emitting this jobspec-update event more than once per test.
     * Note: this means the test plugin will only work for one job without
     *  reloading the plugin
     */
    if (updated)
        return 0;
    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:s}",
                                         "attributes.system.queue",
                                         "updatequeue") < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "update failure");
        return -1;
    }
    updated = true;
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.validate", validate_cb, NULL },
    { "job.state.depend", depend_cb, NULL },
    { "job.state.sched", sched_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "jobspec-update-job-list", tab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
