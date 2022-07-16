/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobspec-default.c - supply defaults for missing jobspec system attrs
 *
 * First set missing keys using values from [policy.jobspec.defaults],
 * then override with values from [queues.<name>.policy.defaults].
 * Post a jobspec-update event to apply the changes.
 *
 * See also:
 *  RFC 33/Flux Job Queues
 *  RFC 21/Job States and Events
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

/* Fetch general defaults and assign to 'defaults' (caller must not free).
 * If no defaults are configured, set defaults to NULL and return success.
 * Return 0 on success.  On parse failure, set 'error' and return -1.
 */
static int get_general_defaults (json_t *conf,
                                 json_t **defaults,
                                 flux_error_t *error)
{
    json_t *o = NULL;
    json_error_t e;

    if (conf) {
        if (json_unpack_ex (conf,
                            &e,
                            0,
                            "{s?{s?{s?{s?o}}}}",
                            "policy",
                              "jobspec",
                                "defaults",
                                  "system", &o) < 0) {
            errprintf (error, "policy.jobspec.defaults.system: %s", e.text);
            return -1;
        }
    }
    *defaults = o;
    return 0;
}

/* Fetch defaults for queue 'name' and assign to 'defaults' (caller must NOT
 * free).  If no defaults are configured, set defaults to NULL and return
 * success.  Return 0 on success.  On parse failure, set 'error' and return -1.
 */
static int get_queue_defaults (json_t *conf,
                               const char *name,
                               json_t **defaults,
                               flux_error_t *error)
{
    json_t *o = NULL;
    json_error_t e;

    if (conf && name) {
        /* N.B. If a queue was named, it is an error if 'name' is missing
         * from [queues], or if [queues] itself is missing;  however, it is
         * not required that the queue configures any policy.
         */
        if (json_unpack_ex (conf,
                            &e,
                            0,
                            "{s:{s:{s?{s?{s?{s?o}}}}}}",
                            "queues", // required
                              name,   // required
                                "policy",
                                  "jobspec",
                                    "defaults",
                                      "system", &o) < 0) {
            errprintf (error,
                       "queues.%s.policy.jobspec.defaults.system: %s",
                       name,
                       e.text);
            return -1;
        }
    }
    *defaults = o;
    return 0;
}

/* Get the queue name from jobspec, or configured default, and set 'queue'.
 * If still unset, set queue to NULL and return success.
 * Return 0 on success.  On parse failure, set 'error' and return -1.
 */
static int get_queue (json_t *jobspec,
                      json_t *general_defaults,
                      const char **queue,
                      flux_error_t *error)
{
    const char *s = NULL;
    json_error_t e;

    if (jobspec) {
        // N.B. jobspec has already been validated so this must succeed
        (void)json_unpack (jobspec, "{s?s}", "queue", &s);
    }
    if (!s && general_defaults) {
        if (json_unpack_ex (general_defaults,
                            &e,
                            0,
                            "{s?s}",
                            "queue", &s) < 0) {
            errprintf (error,
                       "policy.jobspec.defaults.system.queue: %s",
                       e.text);
            return -1;
        }
    }
    *queue = s;
    return 0;
}

/* Create a new object consisting of key-values from o1 and o2,
 * with o2 overwriting values from o1 when keys are present in both.
 * The resulting object must be freed by the caller.
 * N.B. object values are references on values in o1 and o2.
 * Set to NULL if o1 and o2 are NULL and return success.
 * Return 0 on success, -1 on failure.
 */
static int merge_tables (json_t *o1, json_t *o2, json_t **merged)
{
    json_t *o = NULL;

    if (o1 || o2) {
        if (!(o = json_object ()))
            goto error;
        if (o1 && json_object_update (o, o1) < 0)
            goto error;
        if (o2 && json_object_update (o, o2) < 0)
            goto error;
    }
    *merged = o;
    return 0;
error:
    json_decref (o);
    return -1;
}

/* Catch any errors with the config early so that plugin load fails
 * and someone can fix the config before jobs are submitted.
 */
static int validate_config (json_t *conf, flux_error_t *error)
{
    json_t *general_defaults;
    json_t *queue_defaults;
    const char *name;
    json_t *queues = NULL;
    json_t *value;

    if (get_general_defaults (conf, &general_defaults, error) < 0)
        return -1;
    /* get_queue (jobspec=NULL) fetches the default queue, if any.
     * Ensure there are no issues parsing the default queue's policy
     */
    if (get_queue (NULL, general_defaults, &name, error) < 0)
        return -1;
    if (get_queue_defaults (conf, name, &queue_defaults, error) < 0)
        return -1;
    /* Validate each member of [queues] also.
     */
    (void)json_unpack (conf, "{s?o}", "queues", &queues);
    if (queues) {
        if (!json_is_object (queues)) {
            errprintf (error, "queues must be a table");
            return -1;
        }
        json_object_foreach (queues, name, value) {
            if (get_queue_defaults (conf, name, &queue_defaults, error) < 0)
                return -1;
        }
    }
    return 0;
}

/* Build a defaults table that overlays:
 *  [policy.jobspec.defaults.system]
 *  [queues.<name>.policy.defaults.system]
 *
 * and assign it to 'defaults'.  If no defaults are configured, set defaults
 * to NULL and return success.
 * Caller must free the resulting object (N.B. contains refs to conf object).
 * Returns 0 on success.  On failure, set 'error' and return -1.
 */
static int get_policy_defaults (json_t *conf,
                                json_t *jobspec,
                                json_t **defaults,
                                flux_error_t *error)
{
    flux_error_t e;
    json_t *general_defaults;
    json_t *queue_defaults;
    json_t *merged;
    const char *queue;

    if (get_general_defaults (conf, &general_defaults, &e) < 0) {
        errprintf (error, "Error parsing default policy: %s", e.text);
        return -1;
    }
    if (get_queue (jobspec, general_defaults, &queue, &e) < 0) {
        errprintf (error, "Error parsing default queue name: %s", e.text);
        return -1;
    }
    if (get_queue_defaults (conf, queue, &queue_defaults, &e) < 0) {
        /* If this fails it probably means 'queue' is not listed in [queues] or
         * [queues] is missing.  Allow job to proceed for now, if only to
         * avoid breaking fluxion t1006-qmanager-multiqueue.t and other tests.
         */
        queue_defaults = NULL;
    }
    if (merge_tables (general_defaults, queue_defaults, &merged) < 0) {
        errprintf (error, "Out of memory while parsing policy");
        return -1;
    }
    *defaults = merged;
    return 0;
}

/* Generate a jobspec-update event context, containing updates
 * for attributes present in 'defaults' but not 'jobspec'.
 * If there are no updates, return an empty object.
 * On error, return NULL with errno set.
 */
static json_t *generate_update (json_t *defaults, json_t *jobspec)
{
    const char *key;
    json_t *value;
    json_t *update;
    char nkey[128];

    if (!(update = json_object ()))
        goto nomem;
    json_object_foreach (defaults, key, value) {
        json_t *val = jobspec ? json_object_get (jobspec, key) : NULL;

        /* Special: treat user duration=0 as unset per RFC 14.
         */
        if (val != NULL
            && streq (key, "duration")
            && json_real_value (val) == 0)
            val = NULL;

        if (val == NULL) {
            snprintf (nkey, sizeof (nkey), "attributes.system.%s", key);

            /* Special: RFC 33 allows duration to be configured as an
             * FSD string, but it must only appear as a number in the jobspec.
             */
            if (streq (key, "duration") && json_is_string (value)) {
                double d = 0;
                json_t *dv;
                (void)fsd_parse_duration (json_string_value (value), &d);
                if (!(dv = json_real (d))
                    || json_object_set_new (update, nkey, dv) < 0) {
                    json_decref (dv);
                    goto nomem;
                }
            }
            else {
                if (json_object_set (update, nkey, value) < 0)
                    goto nomem;
            }
        }
    }
    return update;
nomem:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (json_decref, update);
    return NULL;
}

static int create_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_job_state_t state;
    json_t *conf = NULL;
    json_t *jobspec = NULL;
    json_t *defaults = NULL;
    json_t *update = NULL;
    flux_error_t error;
    int rc = -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i s:{s:{s?o}}}",
                                "state", &state,
                                "jobspec",
                                  "attributes",
                                    "system", &jobspec) < 0) {
        flux_jobtap_reject_job (p,
                                args,
                                "error unpacking job.create arguments: %s",
                                flux_plugin_arg_strerror (args));
        goto error;
    }

    /* If state is not NEW, this is a job manager/flux restart and any
     * defaults will have already been replayed from the KVS.
     */
    if (state != FLUX_JOB_STATE_NEW)
        goto success;

    (void)flux_conf_unpack (flux_get_conf (h), NULL, "o", &conf);

    /* Construct defaults dict by overlaying general and queue specific
     * configured defaults.
     */
    if (get_policy_defaults (conf, jobspec, &defaults, &error) < 0) {
        flux_jobtap_reject_job (p, args, "%s", error.text);
        flux_log (h, LOG_ERR, "jobspec-default: %s", error.text);
        goto error;
    }

    /* If no defaults are configured, there is nothing to do.
     */
    if (!defaults)
        goto success;

    /* Walk the configured default system attributes.
     * If an attribute appears in jobspec, leave it alone.
     * If it is missing from jobspec, add it to the 'update' object.
     */
    if (!(update = generate_update (defaults, jobspec))) {
        flux_jobtap_reject_job (p,
                                args,
                                "error creating jobspec-update context: %s",
                                 strerror (errno));
        goto error;
    }

    /* Post jobspec-update event, if updates were generated.
     */
    if (json_object_size (update) > 0) {
        if (flux_jobtap_event_post_pack (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "jobspec-update",
                                         "O",
                                         update) < 0) {
            flux_jobtap_reject_job (p,
                                    args,
                                    "failed to post jobspec-update: %s",
                                    strerror (errno));
            goto error;
        }
    }
success:
    rc = 0;
error:
    ERRNO_SAFE_WRAP (json_decref, defaults);
    ERRNO_SAFE_WRAP (json_decref, update);
    return rc;
}

static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    flux_error_t error;
    json_t *conf;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "conf", &conf) < 0) {
        errprintf (&error,
                   "error unpacking conf.update arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }
    if (validate_config (conf, &error) < 0)
        goto error;
    return 0;
error:
    return flux_jobtap_error (p, args, "%s", error.text);
}

static const struct flux_plugin_handler tab[] = {
    { "job.create", create_cb, NULL },
    { "conf.update", conf_update_cb, NULL },
    { 0 }
};

int jobspec_default_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_register (p, ".jobspec-default", tab);
}

// vi:ts=4 sw=4 expandtab
