/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobspec-update.c - test flux_jobtop_jobspec_update_pack(3)
 */

#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"

static int get_and_update_jobspec_name (flux_error_t *errp,
                                        flux_plugin_t *p,
                                        flux_plugin_arg_t *args,
                                        const char **cur_namep,
                                        char *name)
{
    flux_jobid_t id;
    const char *current_name = NULL;
    char *copy = NULL;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s?{s?{s?s}}}}}",
                                "id", &id,
                                "jobspec",
                                "attributes",
                                "system",
                                "job",
                                "name",
                                &current_name) < 0) {
        errprintf (errp,
                   "failed to unpack job name: %s",
                   flux_plugin_arg_strerror (args));
        return -1;
    }

    /* flux_jobtap_jobspec_update_id_pack() should fail here, since this
     * function is always called in the context of a jobtap callback:
     */
    if (flux_jobtap_jobspec_update_id_pack (p,
                                            id,
                                            "{s:s}",
                                            "attributes.system.foo",
                                            "bar") == 0) {
        errprintf (errp,
                   "flux_jobtap_jobspec_update_id_pack() unexpected success");
        return -1;
    }

    if (current_name && !(copy = strdup (current_name))) {
        errprintf (errp, "failed to copy job name");
        return -1;
    }

    /*  Update job name in jobspec and ensure it doesn't change in this
     *  function.
     */
    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:s}",
                                         "attributes.system.job.name",
                                         name) < 0) {
        errprintf (errp,
                   "flux_jobtap_jobspec_update_pack: %s",
                   strerror (errno));
        goto error;
    }
    /*  Ensure name hasn't changed after update is posted.
     */
    if (copy && current_name && !(streq (current_name, copy))) {
        errprintf (errp, "unpacked job name failed to match after update");
        goto error;
    }
    /*  jobspec update with key not starting with attributes. should fail:
     */
    if (flux_jobtap_jobspec_update_pack (p, "{s:s}", "foo.bar", "baz") == 0) {
        errprintf (errp,
                   "update key not starting with attributes. not rejected");
        goto error;
    }
    /*  Add a second key to update in another call:
     */
    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:i}",
                                         "attributes.system.update-test",
                                         1) < 0) {
        errprintf (errp,
                   "flux_jobtap_jobspec_update_pack: %s",
                   strerror (errno));
        goto error;
    }
    if (cur_namep)
        *cur_namep = current_name;
    free (copy);
    return 0;
error:
    free (copy);
    return -1;
}

static int update_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    json_t *updates = NULL;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "updates", &updates) < 0) {
        flux_jobtap_reject_job (p,
                                args,
                                "job.update: %s",
                                flux_plugin_arg_strerror (args));
    }
    return 0;
}

static int new_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    flux_error_t error;
    if (get_and_update_jobspec_name (&error, p, args, NULL, "new") < 0)
        flux_jobtap_reject_job (p, args, "jobspec-update: %s", error.text);
    return 0;
}

static int priority_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    flux_error_t error;
    if (get_and_update_jobspec_name (&error, p, args, NULL, "priority") < 0)
        flux_jobtap_reject_job (p, args, "jobspec-update: %s", error.text);
    return 0;
}


static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    flux_error_t error;
    if (get_and_update_jobspec_name (&error, p, args, NULL, "validated") < 0)
        flux_jobtap_reject_job (p, args, "jobspec-update: %s", error.text);
    return 0;
}

static int depend_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    const char *name;
    flux_error_t error;

    if (get_and_update_jobspec_name (&error, p, args, &name, "depend") < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update",
                                     0,
                                     "get_and_update_name failed: %s",
                                     error.text);
        return -1;
    }
    /*  Ensure jobspec was updated during validate
     */
    if (!name) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "expected job name was NULL");
        return -1;
    }
    if (!streq (name, "new")) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "expected job name 'validated' got %s",
                                     name);
        return -1;
    }
    return 0;
}

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    /*  Jobspec update after RUN is expected to fail
     */
    if (flux_jobtap_jobspec_update_pack (p,
                                         "{s:i}",
                                         "attributes.system.run-update",
                                         1) == 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "jobspec-update", 0,
                                     "expected update failure, got success");
    }
    return 0;
}

static void update_msg_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    flux_plugin_t *p = arg;
    flux_jobid_t id;
    json_t *update;

    if (flux_msg_unpack (msg, "{s:I s:o}", "id", &id, "update", &update) < 0
        || flux_jobtap_jobspec_update_id_pack (p, id, "O", update) < 0)
        flux_jobtap_raise_exception (p, id, "test", 0, "update failed");
    flux_respond (h, msg, NULL);
}

static const struct flux_plugin_handler tab[] = {
    { "job.new", new_cb, NULL },
    { "job.update", update_cb, NULL },
    { "job.validate", validate_cb, NULL },
    { "job.state.priority", priority_cb, NULL },
    { "job.state.depend", depend_cb, NULL },
    { "job.state.run", run_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "jobspec-update", tab) < 0)
        return -1;
    if (flux_jobtap_service_register (p, "update", update_msg_cb, p) < 0)
        flux_log_error (flux_jobtap_get_flux (p),
                        "flux_jobtap_service_register");
    return 0;
}

// vi:ts=4 sw=4 expandtab
