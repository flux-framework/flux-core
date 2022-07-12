/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dependency-test.c - keep jobs in depend state and wait for
 *  an RPC to release
 */

#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>


static void remove_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    flux_jobid_t id;
    const char *description = NULL;
    flux_plugin_t *p = arg;

    if (flux_request_unpack (msg, NULL,
                             "{s:I s:s}",
                             "id", &id,
                             "description", &description) < 0) {
        flux_log_error (h, "failed to unpack dependency-test.remove msg");
        goto error;
    }
    if (flux_jobtap_dependency_remove (p, id, description) < 0)
        goto error;
    if (flux_jobtap_job_aux_set (p, id, description, NULL, NULL) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "flux_respond");
    return;
error:
    flux_respond_error (h, msg, errno, flux_msg_last_error (msg));
}

static void check_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    flux_jobid_t id;
    const char *name = NULL;
    flux_plugin_t *p = arg;

    if (flux_request_unpack (msg, NULL,
                             "{s:I s:s}",
                             "id", &id,
                             "name", &name) < 0) {
        flux_log_error (h, "failed to unpack dependency-test check msg");
        goto error;
    }
    if (flux_jobtap_job_aux_get (p, id, name) != p) {
        errno = ENOENT;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0) {
        flux_log_error (h, "flux_respond");
        goto error;
    }
    return;
error:
    flux_respond_error (h, msg, errno, NULL);
}

static int dependency_test_cb (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *arg)
{
    flux_jobid_t id;
    const char *name = NULL;
    int remove = 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:s s?i}}",
                                "id", &id,
                                "dependency",
                                "value", &name,
                                "remove", &remove) < 0) {
        return flux_jobtap_reject_job (p, args,
                                       "failed to unpack dependency args: %s",
                                       flux_plugin_arg_strerror (args));
    }

    /* Associate some plugin state with the job so we can detect
     *  successful plugin state creation in testing.
     */
    if (flux_jobtap_job_aux_set (p, id, name, p, NULL) < 0)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobap_job_aux_set failed: %s",
                                        strerror (errno));

    if (flux_jobtap_dependency_add (p, id, name) < 0) {
        flux_log_error (flux_jobtap_get_flux (p),
                        "flux_jobtap_dependency_add (%s)",
                        name);
        return -1;
    }
    if (remove) {
        if (flux_jobtap_dependency_remove (p, id, name) < 0)
            return flux_jobtap_reject_job (p, args,
                                           "dependency_remove: %s",
                                            strerror (errno));
        if (flux_jobtap_job_aux_set (p, id, name, NULL, NULL) < 0)
            return flux_jobtap_reject_job (p, args,
                                           "flux_jobtap_job_aux_set: %s",
                                            strerror (errno));
    }
    return 0;
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    const char *description = NULL;
    flux_jobid_t id;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s:{s?s}}}}",
                                "id", &id,
                                "jobspec",
                                "attributes",
                                "system",
                                "dependency-test", &description) < 0) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "dependency-test", 0,
                                     "failed to unpack dependency-test args");
        return -1;
    }
    if (description) {
        if (flux_jobtap_dependency_add (p, id, description) < 0) {
            flux_jobtap_raise_exception (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "dependency-test", 0,
                                         "dependency_add: %s",
                                         strerror (errno));
            return -1;
        }
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.dependency.test", dependency_test_cb, NULL },
    { "job.state.depend",    depend_cb,          NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "dependency-test", tab) < 0
        || flux_jobtap_service_register (p, "remove", remove_cb, p) < 0
        || flux_jobtap_service_register (p, "check", check_cb, p) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
