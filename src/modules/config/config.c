/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* config.c - broker configuration
 *
 * The broker parses the configuration, if any, before bootstrap begins.
 * The broker caches this configuration object and also records the config
 * directory path, if any, in the config.path broker attribute.
 * Later, when modules are started (including this one), they receive a
 * copy of the broker's config object.
 *
 * There is no default config directory path, so by default the attribute
 * is not set and the config object is empty {}.
 *
 * This module fetches the config.path attribute value, if any, and the
 * current config object, at start up.  It offers the following RPC methods:
 *
 * config.get
 *   fetch the current config object, used by flux-config get
 *
 * config.reload
 *   Parse the TOML files at the config path (if set).
 *   Send a config-reload RPC to all loaded modules and the broker.
 *
 * config.load
 *   Replace the current config object with one provided in the request.
 *   Send a config-reload RPC to all loaded modules and the broker.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/broker/module.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"


struct brokercfg {
    flux_t *h;
    char *path;
    flux_msg_handler_t **handlers;
};

/* Send the config-reload RPC to the named module.
 * This works for the broker too since it implements broker.config-reload.
 */
static int update_one_module (flux_t *h,
                              const char *name,
                              const flux_conf_t *conf,
                              flux_error_t *errp)
{
    char topic[1024];
    json_t *o;
    flux_future_t *f = NULL;
    int rc = -1;

    if (snprintf (topic,
                  sizeof (topic),
                  "%s.config-reload",
                  name) >= sizeof (topic)) {
        errprintf (errp, "buffer overflow");
        errno = EOVERFLOW;
        goto done;
    }
    if (flux_conf_unpack (conf, NULL, "o", &o) < 0
        || !(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0, "O", o))
        || (flux_future_get (f, NULL) < 0 && errno != ENOSYS)) {
        errprintf (errp, "%s", future_strerror (f, errno));
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Get the current list of loaded modules and update them all,
 * plus the broker.
 */
static int update_all_modules (flux_t *h,
                               const flux_conf_t *conf,
                               flux_error_t *errp)
{
    flux_future_t *f;
    json_t *mods;
    size_t index;
    json_t *entry;
    const char *name;
    flux_error_t error;
    int rc = -1;

    if (!(f = flux_rpc (h, "module.list", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "{s:o}", "mods", &mods) < 0) {
        errprintf (errp, "module.list: %s", future_strerror (f, errno));
        goto done;
    }
    json_array_foreach (mods, index, entry) {
        if (json_unpack (entry, "{s:s}", "name", &name) < 0) {
            errprintf (errp, "malformed module.list response");
            errno = EPROTO;
            goto done;
        }
        if (streq (name, "config"))
            continue;
        if (update_one_module (h, name, conf, &error) < 0) {
            errprintf (errp, "error updating %s: %s", name, error.text);
            goto done;
        }
    }
    if (update_one_module (h, "broker", conf, &error) < 0) {
        errprintf (errp, "error updating broker: %s", error.text);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy(f);
    return rc;
}

static bool config_equal (const flux_conf_t *c1, const flux_conf_t *c2)
{
    json_t *o1;
    json_t *o2;

    if (flux_conf_unpack (c1, NULL, "o", &o1) == 0
        && flux_conf_unpack (c2, NULL, "o", &o2) == 0
        && json_equal (o1, o2))
        return true;
    return false;
}

/* Handle request to re-parse config object from TOML config files.
 * Initiate reload of config in all loaded modules.
 */
static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct brokercfg *cfg = arg;
    flux_error_t error;
    flux_error_t rerror;
    flux_conf_t *conf;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errprintf (&error, "error decoding config.reload request");
        goto error;
    }
    if (cfg->path) {
        if (!(conf = flux_conf_parse (cfg->path, &rerror))) {
            errprintf (&error, "Config file error: %s", rerror.text);
            goto error;
        }
        if (config_equal (flux_get_conf (h), conf))
            flux_conf_decref (conf);
        else {
            if (flux_set_conf_new (h, conf) < 0) {
                errprintf (&error,
                           "Error caching config object: %s",
                           strerror (errno));
                flux_conf_decref (conf);
                goto error;
            }
            if (update_all_modules (h, conf, &error) < 0)
                goto error;
        }
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config.reload request");
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
    flux_error_t error;
    json_t *o;
    flux_conf_t *conf;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0
        || !(conf = flux_conf_pack ("O", o))) {
        errprintf (&error, "error decoding config.load request");
        goto error;
    }
    if (config_equal (flux_get_conf (h), conf))
        flux_conf_decref (conf);
    else {
        if (flux_set_conf_new (h, conf) < 0) {
            errprintf (&error,
                       "Error caching config object: %s",
                       strerror (errno));
            flux_conf_decref (conf);
            goto error;
        }
        if (update_all_modules (h, conf, &error) < 0)
            goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config.load request");
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to config.load request");
}

/* Handle request to fetch the config object.
 */
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

static void brokercfg_destroy (struct brokercfg *cfg)
{
    if (cfg) {
        int saved_errno = errno;
        flux_msg_handler_delvec (cfg->handlers);
        free (cfg->path);
        free (cfg);
        errno = saved_errno;
    }
}

static struct brokercfg *brokercfg_create (flux_t *h, const char *path)
{
    struct brokercfg *cfg;

    if (!(cfg = calloc (1, sizeof (*cfg))))
        goto error;
    cfg->h = h;
    if (path) {
        if (!(cfg->path = strdup (path)))
            goto error;
    }
    if (flux_msg_handler_addvec (h, htab, cfg, &cfg->handlers) < 0)
        goto error;
    return cfg;
error:
    brokercfg_destroy (cfg);
    return NULL;
}

static int mod_main (flux_t *h, int argc, char *argv[])
{
    struct brokercfg *cfg;
    int rc = -1;

    if (!(cfg = brokercfg_create (h, flux_attr_get (h, "config.path")))) {
        flux_log_error (h, "Error creating config context");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    brokercfg_destroy (cfg);
    return rc;
}

struct module_builtin builtin_config = {
    .name = "config",
    .main = mod_main,
};


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
