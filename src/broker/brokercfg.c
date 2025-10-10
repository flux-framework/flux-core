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

#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"

#include "attr.h"
#include "modhash.h"
#include "brokercfg.h"


struct brokercfg {
    flux_t *h;
    char *path;
    flux_msg_handler_t **handlers;
    modhash_t *modhash;
    flux_future_t *reload_f;
};

/* Parse config object from TOML config files if path is set;
 * otherwise, create an empty config object.  Store the object
 * in ctx->h for later access by flux_get_conf().
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
    if (flux_set_conf_new (h, conf) < 0) {
        errprintf (errp, "Error caching config object");
        goto error;
    }
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
    module = modhash_first (cfg->modhash);
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
        module = modhash_next (cfg->modhash);
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

/* Handle request to replace config object with request payload.
 * Initiate reload of config in all loaded modules.
 */
static void load_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct brokercfg *cfg = arg;
    json_t *o;
    flux_conf_t *conf;
    flux_error_t error;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0
        || !(conf = flux_conf_pack ("O", o))) {
        errprintf (&error, "error decoding config.load request");
        goto error;
    }
    if (flux_set_conf_new (h, conf) < 0) {
        errprintf (&error, "Error caching config object");
        flux_conf_decref (conf);
        goto error;
    }
    if (update_modules_and_respond (h, cfg, msg, &error) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to config.load request");
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
    { FLUX_MSGTYPE_REQUEST,  "config.load", load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "config.get", get_cb, FLUX_ROLE_USER },
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
    if (attr_add (attrs, "config.path", path, ATTR_IMMUTABLE) < 0)
        goto error;
    return cfg;
error:
    brokercfg_destroy (cfg);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
