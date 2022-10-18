/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* brokercfg.c - broker configuration
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"

#include "attr.h"
#include "module.h"
#include "brokercfg.h"


struct brokercfg {
    flux_t *h;
    char *path;
    flux_msg_handler_t **handlers;
    modhash_t *modhash;
    flux_future_t *reload_f;
};

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
        errprintf (error, "%s: %s", key, jerror.text);
        goto inval;
    }
    if (duration) {
        double d;

        if (!json_is_string (duration)
            || fsd_parse_duration (json_string_value (duration), &d) < 0) {
            errprintf (error,
                       "%s.defaults.system.duration is not a valid FSD",
                       key);
            goto inval;
        }
    }
    if (queue) {
        if (!json_is_string (queue)) {
            errprintf (error, "%s.defaults.system.queue is not a string", key);
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
        errprintf (error, "%s.%s: %s", key, key2, jerror.text);
        goto inval;
    }
    if (nnodes < -1 || ncores < -1 || ngpus < -1) {
        errprintf (error, "%s.%s: values must be >= -1", key, key2);
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
        errprintf (error, "%s: %s", key, jerror.text);
        goto inval;
    }
    if (duration) {
        double d;

        if (!json_is_string (duration)
            || fsd_parse_duration (json_string_value (duration), &d) < 0) {
            errprintf (error, "%s.duration is not a valid FSD", key);
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
            errprintf (error, "%s.job-size: %s", key, jerror.text);
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
        errprintf (error, "%s: %s", key, jerror.text);
        goto inval;
    }
    if (allow_user) {
        if (!is_string_array (allow_user, NULL)) {
            errprintf (error, "%s.allow-user must be a string array", key);
            goto inval;
        }
    }
    if (allow_group) {
        if (!is_string_array (allow_group, NULL)) {
            errprintf (error, "%s.allow-group must be a string array", key);
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
        errprintf (error, "%s: %s", key, jerror.text);
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

    if (flux_conf_unpack (conf,
                          error,
                          "{s?o}",
                          "policy", &policy) < 0)
        return -1;
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

    if (flux_conf_unpack (conf,
                          error,
                          "{s?o}",
                          "queues", &queues) < 0)
        return -1;
    if (queues) {
        const char *name;
        json_t *entry;

        if (!json_is_object (queues)) {
            errprintf (error, "queues must be a table");
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
                errprintf (error, "queues.%s: %s", name, jerror.text);
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
                               "%s: queue policy includes default queue!",
                               key);
                    goto inval;
                }
            }
            if (requires) {
                const char *banned_property_chars = " \t!&'\"`'|()";
                if (!is_string_array (requires, banned_property_chars)) {
                    errprintf (error,
                               "queues.%s.requires must be an array of %s",
                               name,
                               "property strings (RFC 20)");
                    goto inval;
                }
            }
        }
    }
    if (default_queue) {
        if (!queues || !json_object_get (queues, default_queue)) {
            errprintf (error,
                       "default queue '%s' is not in queues table",
                       default_queue);
            goto inval;
        }
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static int brokercfg_set (flux_t *h,
                          const flux_conf_t *conf,
                          flux_error_t *error)
{
    const char *defqueue;

    if (validate_policy_config (conf, &defqueue, error) < 0
        || validate_queues_config (conf, defqueue, error) < 0)
        return -1;

    if (flux_set_conf (h, conf) < 0) {
        errprintf (error, "Error caching config object");
        return -1;
    }
    return 0;
}

/* Parse config object from TOML config files if path is set;
 * otherwise, create an empty config object.  Store the object
 * in ctx->h for later access by flux_conf_get().
 */
static int brokercfg_parse (flux_t *h,
                            const char *path,
                            flux_error_t *errp)
{
    flux_error_t error;
    flux_conf_t *conf;

    if (path) {
        if (!(conf = flux_conf_parse (path, &error))) {
            errprintf (errp,
                       "Config file error: %s",
                       error.text);
            return -1;
        }
    }
    else {
        if (!(conf = flux_conf_create ())) {
            errprintf (errp, "Error creating config object");
            return -1;
        }
    }
    if (brokercfg_set (h, conf, errp) < 0)
        goto error;
    return 0;
error:
    flux_conf_decref (conf);
    return -1;
}

/* Now that all modules have responded to '<name>.config-reload' request,
 * send a response to the original broker load/reload request.  If errors
 * occurred (other than ENOSYS), include as much diagnostic info as possible
 * in the response.
 */
static void reload_continuation (flux_future_t *cf, void *arg)
{
    const flux_msg_t *msg = flux_future_aux_get (cf, "flux::request");
    struct brokercfg *cfg = arg;
    const char *name;
    flux_future_t *f;
    char errbuf[4096];
    int errnum = 0;
    size_t offset = 0;

    name = flux_future_first_child (cf);
    while (name) {
        f = flux_future_get_child (cf, name);

        if (flux_future_get (f, NULL) < 0 && errno != ENOSYS) {
            if (errnum == 0)
                errnum = errno;
            (void)snprintf (errbuf + offset,
                            sizeof (errbuf) - offset,
                            "%s%s: %s",
                            offset > 0 ? "\n" : "",
                            name,
                            flux_future_error_string (f));
            offset = strlen (errbuf);
        }
        name = flux_future_next_child (cf);
    }
    if (errnum != 0)
        goto error;

    if (flux_respond (cfg->h, msg, NULL) < 0)
        flux_log_error (cfg->h, "reload: flux_respond");
    flux_log (cfg->h, LOG_INFO, "configuration updated");
    flux_future_destroy (cfg->reload_f);
    cfg->reload_f = NULL;
    return;

error:
    if (flux_respond_error (cfg->h, msg, errnum, errbuf) < 0)
        flux_log_error (cfg->h, "reload: flux_respond_error");
    flux_future_destroy (cfg->reload_f);
    cfg->reload_f = NULL;
    flux_log (cfg->h, LOG_ERR, "config reload failed");
}

/* Send a '<name>.config-reload' request to all loaded modules.
 * On success, return a composite future that is fulfilled once all modules
 * have responded.  On failure, return NULL with errno set.
 */
static flux_future_t *reload_module_configs (flux_t *h, struct brokercfg *cfg)
{
    flux_future_t *cf;
    module_t *module;
    json_t *conf;

    if (flux_conf_unpack (flux_get_conf (h), NULL, "o", &conf) < 0)
        return NULL;
    if (!(cf = flux_future_wait_all_create ()))
        return NULL;
    flux_future_set_flux (cf, h);
    module = module_first (cfg->modhash);
    while (module) {
        flux_future_t *f;
        char topic[1024];

        if (snprintf (topic,
                      sizeof (topic),
                      "%s.config-reload",
                      module_get_name (module)) >= sizeof (topic)) {
            errno = EOVERFLOW;
            goto error;
        }
        if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0, "O", conf)))
            goto error;
        if (flux_future_push (cf, module_get_name (module), f) < 0) {
            flux_future_destroy (f);
            goto error;
        }
        module = module_next (cfg->modhash);
    }
    return cf;
error:
    flux_future_destroy (cf);
    return NULL;
}

static int update_modules_and_respond (flux_t *h,
                                       struct brokercfg *cfg,
                                       const flux_msg_t *msg,
                                       flux_error_t *error)
{
    flux_future_t *f;

    if (cfg->reload_f) {
        errprintf (error, "module config-reload in progress, try again later");
        errno = EBUSY;
        return -1;
    }
    if (!(f = reload_module_configs (h, cfg))
        || flux_future_then (f, -1., reload_continuation, cfg) < 0)
        goto error;
    if (flux_future_aux_set (f,
                             "flux::request",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error;
    }
    cfg->reload_f = f;
    return 0;
error:
    errprintf (error, "failed to set up asynchronous module config-reload");
    flux_future_destroy (f);
    return -1;
}

/* Handle request to re-parse config object from TOML config files.
 * If files fail to parse, generate an immediate error response.  Otherwise,
 * initiate reload of config in all loaded modules and respond when complete.
 */
static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct brokercfg *cfg = arg;
    flux_error_t error;

    if (brokercfg_parse (h, cfg->path, &error) < 0
        || update_modules_and_respond (h, cfg, msg, &error) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to config.reload request");
}

static void get_cb (flux_t *h,
                    flux_msg_handler_t *mh,
                    const flux_msg_t *msg,
                    void *arg)
{
    const char *errmsg = NULL;
    flux_error_t error;
    json_t *o;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_conf_unpack (flux_get_conf (h), &error, "o", &o) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "error responding to config.get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to config.get request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "config.reload", reload_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "config.get", get_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void brokercfg_destroy (struct brokercfg *cfg)
{
    if (cfg) {
        int saved_errno = errno;
        flux_msg_handler_delvec (cfg->handlers);
        flux_future_destroy (cfg->reload_f);
        free (cfg->path);
        free (cfg);
        errno = saved_errno;
    }
}

struct brokercfg *brokercfg_create (flux_t *h,
                                    const char *path,
                                    attr_t *attrs,
                                    modhash_t *modhash)
{
    struct brokercfg *cfg;
    flux_error_t error;

    if (!(cfg = calloc (1, sizeof (*cfg))))
        return NULL;
    cfg->h = h;
    cfg->modhash = modhash;
    if (!path)
        path = getenv ("FLUX_CONF_DIR");
    if (path) {
        if (!(cfg->path = strdup (path)))
            goto error;
    }
    if (brokercfg_parse (h, path, &error) < 0) {
        log_msg ("%s", error.text);
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, cfg, &cfg->handlers) < 0)
        goto error;
    if (attr_add (attrs, "config.path", path, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        goto error;
    return cfg;
error:
    brokercfg_destroy (cfg);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
