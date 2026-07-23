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
#include "ccan/str/str.h"

#include "conf_policy.h"

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
                                 json_t **schedulerp,
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
    if (schedulerp)
        *schedulerp = scheduler;
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
        if (validate_policy_json (policy, "policy", &defqueue, NULL, error)
            < 0)
            return -1;
    }
    if (default_queue)
        *default_queue = defqueue;
    return 0;
}

/* Returns true if 'entry' (a queues.NAME config table) declares a 'parent'
 * key, i.e. is a virtual queue. '*parentp' is set to the parent name
 * string (unvalidated) when non-NULL and a parent key is present.
 */
static bool queue_entry_is_virtual (json_t *entry, const char **parentp)
{
    json_t *parent;

    if ((parent = json_object_get (entry, "parent"))
        && json_is_string (parent)) {
        if (parentp)
            *parentp = json_string_value (parent);
        return true;
    }
    return false;
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
        /* Pass 1: per-entry syntax validation
         */
        json_object_foreach (queues, name, entry) {
            json_error_t jerror;
            json_t *policy = NULL;
            json_t *requires = NULL;
            json_t *parent = NULL;

            if (json_unpack_ex (entry,
                                &jerror,
                                0,
                                "{s?o s?o s?s !}",
                                "policy", &policy,
                                "requires", &requires,
                                "parent", &parent) < 0) {
                errprintf (error,
                           "error parsing [queues.%s] config table: %s",
                           name,
                           jerror.text);
                goto inval;
            }
            if (policy) {
                char key[1024];
                const char *defqueue;
                json_t *scheduler = NULL;
                snprintf (key, sizeof (key), "queues.%s.policy", name);
                if (validate_policy_json (policy,
                                          key,
                                          &defqueue,
                                          &scheduler,
                                          error) < 0)
                    return -1;
                if (defqueue) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " 'policy' must not define a default queue!",
                               name);
                    goto inval;
                }
                if (parent && scheduler) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " a virtual queue must not set"
                               " 'policy.scheduler'",
                               name);
                    goto inval;
                }
            }
            if (requires) {
                if (parent) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " a virtual queue must not set 'requires'",
                               name);
                    goto inval;
                }
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
        /* Pass 2: cross-queue virtual queue relationships. This requires
         * all entries to have already passed syntax validation above, and
         * looks up parent entries directly out of the [queues] table
         * (json_object_get, not the job-manager's queues.[ch] table, which
         * does not exist yet when config is first validated).
         */
        json_object_foreach (queues, name, entry) {
            const char *parent_name;

            if (queue_entry_is_virtual (entry, &parent_name)) {
                json_t *parent_entry;

                if (streq (parent_name, name)) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " parent queue is itself",
                               name);
                    goto inval;
                }
                if (!(parent_entry = json_object_get (queues, parent_name))) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " parent queue '%s' is not configured",
                               name,
                               parent_name);
                    goto inval;
                }
                if (queue_entry_is_virtual (parent_entry, NULL)) {
                    errprintf (error,
                               "error parsing [queues.%s] config table:"
                               " parent queue '%s' is itself a virtual"
                               " queue",
                               name,
                               parent_name);
                    goto inval;
                }
            }
        }
    }
    if (default_queue) {
        /* A virtual queue may be the default queue, so only require that
         * the name appear in [queues], not that it be non-virtual.
         */
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

int conf_policy_validate (const flux_conf_t *conf, flux_error_t *error)
{
    const char *defqueue;

    if (validate_policy_config (conf, &defqueue, error) < 0
        || validate_queues_config (conf, defqueue, error) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
