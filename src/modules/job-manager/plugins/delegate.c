/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*
 * Jobtap plugin for delegating jobs to another Flux instance.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <flux/jobtap.h>
#include <inttypes.h>
#include <jansson.h>
#include <stdint.h>

/*
 * Callback firing when job has completed.
 */
static void wait_callback (flux_future_t *f, void *arg)
{
    flux_plugin_t *p = arg;
    json_int_t *id;
    bool success;
    const char *errstr;

    if (!(id = flux_future_aux_get (f, "flux::jobid"))) {
        return;
    }
    if (flux_job_wait_get_status (f, &success, &errstr) < 0) {
        flux_jobtap_raise_exception (p,
                                     *id,
                                     "DelegationFailure",
                                     0,
                                     "Could not fetch result of job");
        return;
    }
    if (success) {
        flux_jobtap_raise_exception (p, *id, "DelegationSuccess", 0, "");
    } else {
        flux_jobtap_raise_exception (p,
                                     *id,
                                     "DelegationFailure",
                                     0,
                                     "errstr %s",
                                     errstr);
    }
    flux_future_destroy (f);
}

/*
 * Callback firing when job has been submitted and ID is ready.
 */
static void submit_callback (flux_future_t *f, void *arg)
{
    flux_t *h, *delegated_h;
    flux_plugin_t *p = arg;
    json_int_t *orig_id;
    flux_jobid_t delegated_id;
    flux_future_t *wait_future = NULL;
    const char *errstr;

    if (!(h = flux_jobtap_get_flux (p))) {
        flux_future_destroy (f);
        return;
    } else if (!(orig_id = flux_future_aux_get (f, "flux::jobid"))) {
        flux_log_error (h, "in submit callback: couldn't get jobid");
        flux_future_destroy (f);
        return;
    }
    if (!(delegated_h = flux_future_get_flux (f))
        || flux_job_submit_get_id (f, &delegated_id) < 0
        || !(wait_future = flux_job_wait (delegated_h, delegated_id))
        || flux_future_aux_set (wait_future, "flux::jobid", orig_id, NULL) < 0
        || flux_future_then (wait_future, -1, wait_callback, p) < 0
        || flux_jobtap_event_post_pack (p,
                                        *orig_id,
                                        "delegated",
                                        "{s:I}",
                                        "jobid",
                                        (json_int_t)delegated_id)
               < 0) {
        if (!(errstr = flux_future_error_string (f))) {
            errstr = "";
        }
        flux_log_error (h,
                        "%" JSON_INTEGER_FORMAT
                        ": submission to specified Flux instance failed",
                        *orig_id);
        flux_jobtap_raise_exception (p, *orig_id, "DelegationFailure", 0, errstr);
        flux_future_destroy (wait_future);
        flux_future_destroy (f);
        return;
    }
    flux_future_destroy (f);
}

/*
 * Remove all dependencies from jobspec.
 *
 * Dependencies may reference jobids that the instance the job is
 * being sent to does not recognize.
 *
 * Also, if the 'delegate' dependency in particular were not removed,
 * one of two things would happen. If the instance the job is sent to
 * does not have this jobtap plugin loaded, then the job would be
 * rejected. Otherwise, if the instance DOES have this jobtap plugin
 * loaded, it would attempt to delegate to itself in an infinite
 * loop.
 */
static char *remove_dependency_and_encode (json_t *jobspec)
{
    char *encoded_jobspec;
    json_t *dependency_list = NULL;

    if (!(jobspec = json_deep_copy (jobspec))) {
        return NULL;
    }
    if (json_unpack (jobspec,
                     "{s:{s:{s:o}}}",
                     "attributes",
                     "system",
                     "dependencies",
                     &dependency_list)
            < 0
        || json_array_clear (dependency_list) < 0) {
        json_decref (jobspec);
        return NULL;
    }
    encoded_jobspec = json_dumps (jobspec, 0);
    json_decref (jobspec);
    return encoded_jobspec;
}

/*
 * Handle job.dependency.delegate requests
 */
static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    flux_t *h = flux_jobtap_get_flux (p);
    json_int_t *id;
    flux_t *delegated;
    const char *uri;
    json_t *jobspec;
    char *encoded_jobspec = NULL;
    flux_future_t *jobid_future = NULL;

    if (!h || !(id = malloc (sizeof (json_int_t)))) {
        return flux_jobtap_reject_job (p,
                                       args,
                                       "error processing delegate: %s",
                                       flux_plugin_arg_strerror (args));
    }
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:s} s:o}",
                                "id",
                                id,
                                "dependency",
                                "value",
                                &uri,
                                "jobspec",
                                &jobspec)
            < 0
        || flux_jobtap_job_aux_set (p, *id, "flux::jobid", id, free) < 0) {
        free (id);
        return flux_jobtap_reject_job (p,
                                       args,
                                       "error processing delegate: %s",
                                       flux_plugin_arg_strerror (args));
    }
    if (!(delegated = flux_open (uri, 0))) {
        flux_log_error (h, "%" JSON_INTEGER_FORMAT ": could not open URI %s", *id, uri);
        return -1;
    }
    if (flux_jobtap_dependency_add (p, *id, "delegated") < 0
        || flux_jobtap_job_aux_set (p,
                                    *id,
                                    "flux::delegated_handle",
                                    delegated,
                                    (flux_free_f)flux_close)
               < 0
        || flux_set_reactor (delegated, flux_get_reactor (h)) < 0) {
        flux_log_error (h, "%" JSON_INTEGER_FORMAT ": flux_jobtap_dependency_add", *id);
        flux_close (delegated);
        return -1;
    }
    // submit the job to the specified instance and attach a callback for fetching the
    // ID
    if (!(encoded_jobspec = remove_dependency_and_encode (jobspec))
        || !(jobid_future =
                 flux_job_submit (delegated, encoded_jobspec, 16, FLUX_JOB_WAITABLE))
        || flux_future_then (jobid_future, -1, submit_callback, p) < 0
        || flux_future_aux_set (jobid_future, "flux::jobid", id, NULL) < 0) {
        flux_log_error (h,
                        "%" JSON_INTEGER_FORMAT
                        ": could not delegate job to specified Flux "
                        "instance",
                        *id);
        flux_future_destroy (jobid_future);
        free (encoded_jobspec);
        return -1;
    }
    free (encoded_jobspec);
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    {"job.dependency.delegate", depend_cb, NULL},
    {0},
};

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_register (p, "delegate", tab);
}
