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

#include <string.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>

static bool reconcile;

static void query_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    flux_jobid_t id;
    const char *description;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:I s:s}",
                             "id", &id,
                             "description", &description) < 0
        || (rc = flux_jobtap_dependency_check (arg, id, description)) < 0) {
        flux_respond_error (h, msg, errno, NULL);
        return;
    }
    if (flux_respond_pack (h, msg, "{s:i}", "outstanding", rc) < 0)
        flux_log_error (h, "flux_respond_pack");
}

/* A removal is visible immediately, even before its event is processed. */
static int event_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    flux_jobid_t id;
    const char *description;
    bool add = !strcmp (topic, "job.event.dependency-add");

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s:s}}}",
                                "id", &id,
                                "entry",
                                "context",
                                "description", &description) < 0)
        goto error;
    if (strcmp (description, "query-timing") != 0)
        return 0;
    if (flux_jobtap_dependency_check (p, id, description) != (add ? 1 : 0))
        goto error;
    if (add
        && (flux_jobtap_dependency_remove (p, id, description) < 0
            || flux_jobtap_dependency_check (p,
                                             FLUX_JOBTAP_CURRENT_JOB,
                                             description) != 0))
        goto error;
    return 0;
error:
    return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                        "test", 0,
                                        "dependency query event timing failed");
}


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
    /* Remove the dependency 'remove' times, to test repeated removal.
     */
    for (int i = 0; i < remove; i++) {
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

/* Reconcile description-only dependencies when jobs are rediscovered. */
static int new_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    const char *description = NULL;
    flux_jobid_t id;
    int rc;

    if (!reconcile)
        return 0;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s?{s?s}}}}",
                                "id", &id,
                                "jobspec",
                                "attributes",
                                "system",
                                "dependency-test", &description) < 0)
        goto error;
    if (description) {
        if ((rc = flux_jobtap_dependency_check (p,
                                                FLUX_JOBTAP_CURRENT_JOB,
                                                description)) < 0
            || (rc == 1
                && flux_jobtap_dependency_remove (p, id, description) < 0))
            goto error;
    }
    return 0;
error:
    return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                        "dependency-test", 0,
                                        "dependency reconciliation failed");
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    const char *description = NULL;
    const char *absent = NULL;
    flux_jobid_t id;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s?{s?s s?s}}}}",
                                "id", &id,
                                "jobspec",
                                "attributes",
                                "system",
                                "dependency-test", &description,
                                "dependency-test-absent", &absent) < 0) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "dependency-test", 0,
                                     "failed to unpack dependency-test args");
        return -1;
    }
    if (description && !reconcile) {
        if (!strcmp (description, "query-timing")
            && flux_jobtap_job_subscribe (p, id) < 0)
            goto error;
        if (flux_jobtap_dependency_add (p, id, description) < 0)
            goto error;
        if (!strcmp (description, "query-timing")) {
            const char *pair = "query-timing-pair";

            /* Check a queued add, then an add and remove queued together.
             * The event callback checks removal of an applied dependency.
             */
            if (flux_jobtap_dependency_check (p, id, description) != 1
                || flux_jobtap_dependency_add (p, id, pair) < 0
                || flux_jobtap_dependency_check (p,
                                                 FLUX_JOBTAP_CURRENT_JOB,
                                                 pair) != 1
                || flux_jobtap_dependency_remove (p, id, pair) < 0
                || flux_jobtap_dependency_check (p, id, pair) != 0) {
                errno = EINVAL;
                goto error;
            }
        }
    }
    /* Removing a dependency that was never added must fail with ENOENT.
     */
    if (absent) {
        if (flux_jobtap_dependency_remove (p, id, absent) == 0
            || errno != ENOENT) {
            flux_jobtap_raise_exception (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "dependency-test", 0,
                                         "dependency_remove (%s) did not"
                                         " fail with ENOENT",
                                         absent);
            return -1;
        }
    }
    return 0;
error:
    return flux_jobtap_raise_exception (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                        "dependency-test", 0,
                                        "dependency operation: %s",
                                        strerror (errno));
}

static const struct flux_plugin_handler tab[] = {
    { "job.new",             new_cb,            NULL },
    { "job.dependency.test", dependency_test_cb, NULL },
    { "job.state.depend",    depend_cb,          NULL },
    { "job.event.dependency-add",    event_cb, NULL },
    { "job.event.dependency-remove", event_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    int value = 0;

    if (flux_plugin_conf_unpack (p, "{s?b}", "reconcile", &value) < 0
        && errno != ENOENT)
        return -1;
    reconcile = value;
    if (flux_plugin_register (p, "dependency-test", tab) < 0
        || flux_jobtap_service_register (p, "remove", remove_cb, p) < 0
        || flux_jobtap_service_register (p, "query", query_cb, p) < 0
        || flux_jobtap_service_register (p, "check", check_cb, p) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
