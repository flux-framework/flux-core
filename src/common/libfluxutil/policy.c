/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* policy.c - parse and validate RFC 33 queue/policy config tables
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"

#include "policy.h"

static int validate_policy_jobspec (json_t *o,
                                    const char *key,
                                    const char **default_queue,
                                    flux_error_t *error)
{
    json_error_t jerror;
    json_t *duration = NULL;
    json_t *queue = NULL;

    if (json_unpack_ex (o,
                        &jerror,
                        0,
                        "{s?{s?{s?o s?o !} !} !}",
                        "defaults",
                          "system",
                            "duration", &duration,
                            "queue", &queue) < 0) {
        errprintf (error,
                   "error parsing [%s] config table: %s", key, jerror.text);
        goto inval;
    }
    if (duration) {
        double d;

        if (!json_is_string (duration)
            || fsd_parse_duration (json_string_value (duration), &d) < 0) {
            errprintf (error,
                       "error parsing [%s] config table:"
                       " 'defaults.system.duration' is not a valid FSD",
                       key);
            goto inval;
        }
    }
    if (queue) {
        if (!json_is_string (queue)) {
            errprintf (error,
                       "error parsing [%s] config table:"
                       " 'defaults.system.queue' is not a string",
                       key);
            goto inval;
        }
    }
    if (default_queue)
        *default_queue = queue ? json_string_value (queue) : NULL;
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static int validate_policy_limits_job_size (json_t *o,
                                            const char *key,
                                            const char *key2,
                                            flux_error_t *error)
{
    json_error_t jerror;
    int nnodes = -1;
    int ncores = -1;
    int ngpus = -1;

    if (json_unpack_ex (o,
                        &jerror,
                        0,
                        "{s?i s?i s?i !}",
                        "nnodes", &nnodes,
                        "ncores", &ncores,
                        "ngpus", &ngpus) < 0) {
        errprintf (error,
                   "error parsing [%s.%s] config table: %s",
                   key,
                   key2,
                   jerror.text);
        goto inval;
    }
    if (nnodes < -1 || ncores < -1 || ngpus < -1) {
        errprintf (error,
                   "error parsing [%s.%s] config table:"
                   " values must be >= -1",
                   key,
                   key2);
        goto inval;
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static int validate_policy_limits (json_t *o,
                                   const char *key,
                                   flux_error_t *error)
{
    json_error_t jerror;
    json_t *job_size = NULL;
    json_t *duration = NULL;

    if (json_unpack_ex (o,
                        &jerror,
                        0,
                        "{s?o s?o !}",
                        "job-size", &job_size,
                        "duration", &duration) < 0) {
        errprintf (error,
                   "error parsing [%s] config table: %s",
                   key,
                   jerror.text);
        goto inval;
    }
    if (duration) {
        double d;

        if (!json_is_string (duration)
            || fsd_parse_duration (json_string_value (duration), &d) < 0) {
            errprintf (error,
                       "error parsing [%s] config table:"
                       " 'duration' is not a valid FSD",
                       key);
            goto inval;
        }
    }
    if (job_size) {
        json_t *min = NULL;
        json_t *max = NULL;

        if (json_unpack_ex (job_size,
                            &jerror,
                            0,
                            "{s?o s?o !}",
                            "min", &min,
                            "max", &max) < 0) {
            errprintf (error,
                       "error parsing [%s.job-size] config table: %s",
                       key,
                       jerror.text);
            goto inval;
        }
        if (min) {
            if (validate_policy_limits_job_size (min, key, "min", error) < 0)
                goto inval;
        }
        if (max) {
            if (validate_policy_limits_job_size (max, key, "max", error) < 0)
                goto inval;
        }
    }
    return  0;
inval:
    errno = EINVAL;
    return -1;
}

static bool is_string_array (json_t *o, const char *banned)
{
    size_t index;
    json_t *val;

    if (!json_is_array (o))
        return false;
    json_array_foreach (o, index, val) {
        if (!json_is_string (val))
            return false;
        if (banned) {
            for (int i = 0; banned[i] != '\0'; i++) {
                if (strchr (json_string_value (val), banned[i]))
                    return false;
            }
        }
    }
    return true;
}

static int validate_policy_access (json_t *o,
                                   const char *key,
                                   flux_error_t *error)
{
    json_error_t jerror;
    json_t *allow_user = NULL;
    json_t *allow_group = NULL;

    if (json_unpack_ex (o,
                        &jerror,
                        0,
                        "{s?o s?o !}",
                        "allow-user", &allow_user,
                        "allow-group", &allow_group) < 0) {
        errprintf (error,
                   "error parsing [%s] config table: %s",
                   key,
                   jerror.text);
        goto inval;
    }
    if (allow_user) {
        if (!is_string_array (allow_user, NULL)) {
            errprintf (error,
                       "error parsing [%s] config table:"
                       " 'allow-user' must be a string array",
                       key);
            goto inval;
        }
    }
    if (allow_group) {
        if (!is_string_array (allow_group, NULL)) {
            errprintf (error,
                       "error parsing [%s] config table:"
                       " 'allow-group' must be a string array",
                       key);
            goto inval;
        }
    }
    return  0;
inval:
    errno = EINVAL;
    return -1;
}

/* Validate the policy table as defined by RFC 33.  The table can appear at
 * the top level of the config or within a queues entry.
 */
static int validate_policy_json (json_t *policy,
                                 const char *key,
                                 const char **default_queue,
                                 flux_error_t *error)
{
    json_error_t jerror;
    json_t *jobspec = NULL;
    json_t *limits = NULL;
    json_t *access = NULL;
    json_t *scheduler = NULL;
    const char *defqueue = NULL;
    char key2[1024];

    if (json_unpack_ex (policy,
                        &jerror,
                        0,
                        "{s?o s?o s?o s?o !}",
                        "jobspec", &jobspec,
                        "limits", &limits,
                        "access", &access,
                        "scheduler", &scheduler) < 0) {
        errprintf (error,
                   "error parsing [%s] config table: %s",
                   key,
                   jerror.text);
        errno = EINVAL;
        return -1;
    }
    if (jobspec) {
        snprintf (key2, sizeof (key2), "%s.jobspec", key);
        if (validate_policy_jobspec (jobspec, key2, &defqueue, error) < 0)
            return -1;
    }
    if (limits) {
        snprintf (key2, sizeof (key2), "%s.limits", key);
        if (validate_policy_limits (limits, key2, error) < 0)
            return -1;
    }
    if (access) {
        snprintf (key2, sizeof (key2), "%s.access", key);
        if (validate_policy_access (access, key2, error) < 0)
            return -1;
    }
    if (default_queue)
        *default_queue = defqueue;
    return 0;
}

static int validate_policy_config (const flux_conf_t *conf,
                                   const char **default_queue,
                                   flux_error_t *error)
{
    json_t *policy = NULL;
    const char *defqueue = NULL;
    flux_error_t e;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?o}",
                          "policy", &policy) < 0) {
        errprintf (error, "error parsing [policy] config table: %s", e.text);
        return -1;
    }
    if (policy) {
        if (validate_policy_json (policy, "policy", &defqueue, error) < 0)
            return -1;
    }
    if (default_queue)
        *default_queue = defqueue;
    return 0;
}

static int validate_queues_config (const flux_conf_t *conf,
                                   const char *default_queue,
                                   flux_error_t *error)
{
    json_t *queues = NULL;
    flux_error_t e;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?o}",
                          "queues", &queues) < 0) {
        errprintf (error, "error parsing [queues] config table: %s", e.text);
        return -1;
    }
    if (queues) {
        const char *name;
        json_t *entry;

        if (!json_is_object (queues)) {
            errprintf (error,
                       "error parsing [queues] config table:"
                       " not a table");
            goto inval;
        }
        json_object_foreach (queues, name, entry) {
            json_error_t jerror;
            json_t *policy = NULL;
            json_t *requires = NULL;

            if (json_unpack_ex (entry,
                                &jerror,
                                0,
                                "{s?o s?o !}",
                                "policy", &policy,
                                "requires", &requires) < 0) {
                errprintf (error,
                           "error parsing [queues.%s] config table: %s",
                           name,
                           jerror.text);
                goto inval;
            }
            if (policy) {
                char key[1024];
                const char *defqueue;
                snprintf (key, sizeof (key), "queues.%s.policy", name);
                if (validate_policy_json (policy, key, &defqueue, error) < 0)
                    return -1;
                if (defqueue) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " 'policy' must not define a default queue!",
                               name);
                    goto inval;
                }
            }
            if (requires) {
                const char *banned_property_chars = " \t!&'\"`'|()";
                if (!is_string_array (requires, banned_property_chars)) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " 'requires' must be an array of property"
                               " strings (RFC 20)",
                               name);
                    goto inval;
                }
            }
        }
    }
    if (default_queue) {
        if (!queues || !json_object_get (queues, default_queue)) {
            errprintf (error,
                       "the [policy] config table defines a default queue %s"
                       " that is not in [queues] table",
                       default_queue);
            goto inval;
        }
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

int policy_validate (const flux_conf_t *conf, flux_error_t *error)
{
    const char *defqueue;

    if (validate_policy_config (conf, &defqueue, error) < 0
        || validate_queues_config (conf, defqueue, error) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
