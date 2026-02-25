/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>
#include <flux/core.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/broker/module.h"

bool flux_module_debug_test (flux_t *h, int flag, bool clear)
{
    int *flagsp = flux_aux_get (h, "flux::debug_flags");

    if (!flagsp || !(*flagsp & flag))
        return false;
    if (clear)
        *flagsp &= ~flag;
    return true;
}

int flux_module_set_running (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i}",
                             "status", FLUX_MODSTATE_RUNNING)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

static int module_set_finalizing (flux_t *h,
                                  double timeout,
                                  flux_error_t *errp)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i}",
                             "status", FLUX_MODSTATE_FINALIZING))
        || flux_future_wait_for (f, timeout) < 0
        || flux_rpc_get (f, NULL) < 0) {
        errprintf (errp,
                   "module status (FINALIZING): %s",
                   future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static int module_set_exited (flux_t *h, int errnum, flux_error_t *errp)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i s:i}",
                             "status", FLUX_MODSTATE_EXITED,
                             "errnum", errnum))) {
        errprintf (errp, "module.status (EXITED): %s", strerror (errno));
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int flux_module_config_request_decode (const flux_msg_t *msg,
                                       flux_conf_t **confp)
{
    json_t *o;
    flux_conf_t *conf;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0
        || !(conf = flux_conf_pack ("O", o)))
        return -1;
    if (confp)
        *confp = conf;
    else
        flux_conf_decref (conf);
    return 0;
}

/* Serialize module arguments received in the RFC 5 welcome message
 * as a space delimited string for portability.
 */
static int encode_module_arguments (json_t *args, char **ap, flux_error_t *errp)
{
    size_t index;
    json_t *entry;
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;

    json_array_foreach (args, index, entry) {
        const char *s;
        if ((s = json_string_value (entry))) {
            if ((e = argz_add (&argz, &argz_len, s)) != 0) {
                errno = e;
                goto error;
            }
        }
    }
    argz_stringify (argz, argz_len, ' ');
    *ap = argz;
    return 0;
error:
    ERRNO_SAFE_WRAP (free, argz);
    return -1;
}

/* Set a string in the aux container, making a copy first.
 */
static int set_aux_strdup (flux_t *h,
                           const char *name,
                           const char *val,
                           flux_error_t *errp)
{
    char *cpy;

    if (!(cpy = strdup (val))
        || flux_aux_set (h, name, cpy, (flux_free_f)free) < 0) {
        errprintf (errp, "error setting %s: %s", name, strerror (errno));
        ERRNO_SAFE_WRAP (free, cpy);
        return -1;
    }
    return 0;
}

/* Cache TOML config received in the RFC 5 welcome message.
 */
static int cache_config (flux_t *h, json_t *conf, flux_error_t *errp)
{
    flux_conf_t *cf;

    if (!(cf = flux_conf_pack ("O", conf))
        || flux_set_conf_new (h, cf) < 0) {
        flux_conf_decref (cf);
        errprintf (errp, "error caching config object: %s", strerror (errno));
        return -1;
    }
    return 0;
}

/* Cache immutable broker attributes received in the RFC 5 welcome message.
 */
static int cache_attributes (flux_t *h, json_t *attrs, flux_error_t *errp)
{
    const char *name;
    json_t *o;

    json_object_foreach (attrs, name, o) {
        const char *val = json_string_value (o);
        if (flux_attr_set_cacheonly (h, name, val) < 0) {
            errprintf (errp, "setattr cache %s: %s", name, strerror (errno));
            return -1;
        }
    }
    return 0;
}

int flux_module_initialize (flux_t *h, char **ap, flux_error_t *error)
{
    flux_msg_t *msg;
    json_t *args;
    json_t *attrs;
    json_t *conf;
    const char *name;
    const char *uuid;
    int rc = -1;

    if (!h) {
        errno = EINVAL;
        return errprintf (error, "invalid argument");
    }
    if (!(msg = flux_recv (h, FLUX_MATCH_REQUEST, 0))) {
        errprintf (error, "welcome receive failure: %s", strerror (errno));
        return -1;
    }
    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:o s:o s:s s:s}",
                             "args", &args,
                             "attrs", &attrs,
                             "conf", &conf,
                             "name", &name,
                             "uuid", &uuid) < 0) {
        errprintf (error, "welcome decode failure: %s", strerror (errno));
        goto done;
    }
    if (cache_config (h, conf, error) < 0
        || cache_attributes (h, attrs, error) < 0
        || set_aux_strdup (h, "flux::name", name, error) < 0
        || set_aux_strdup (h, "flux::uuid", uuid, error) < 0)
        goto done;
    if (ap) {
        if (encode_module_arguments (args, ap, error) < 0)
            goto done;
    }
    rc = 0;
done:
    flux_msg_decref (msg);
    return rc;
}

/* Stop the module reactor so the module can be unloaded.
 */
static void module_shutdown_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    flux_reactor_stop (flux_get_reactor (h));
}

/* Notify broker that module is running (just once)
 */
static void module_prepare_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    flux_t *h = arg;
    if (flux_module_set_running (h) < 0)
        flux_log_error (h, "error setting module status to running");
    flux_watcher_stop (w);
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "shutdown",
      module_shutdown_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct module_ctx {
    flux_msg_handler_t **handlers_default;
    flux_msg_handler_t **handlers;
    flux_watcher_t *w_prepare;
};

static void module_ctx_destroy (struct module_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_watcher_destroy (ctx->w_prepare);
        flux_msg_handler_delvec (ctx->handlers);
        flux_msg_handler_delvec (ctx->handlers_default);
        free (ctx);
        errno = saved_errno;
    }
}

static flux_watcher_t *register_prepare (flux_t *h, flux_error_t *errp)
{
    flux_watcher_t *w;
    if (!(w = flux_prepare_watcher_create (flux_get_reactor (h),
                                           module_prepare_cb,
                                           h))) {
        errprintf (errp, "error creating prepare watcher");
        return NULL;
    }
    flux_watcher_start (w);
    return w;
}

static int subscribe_events (flux_t *h, const char *name, flux_error_t *errp)
{
    char *topic = NULL;
    if (asprintf (&topic, "%s.stats-clear", name) < 0
        || flux_event_subscribe (h, topic) < 0) {
        errprintf (errp,
                   "error subscribing to stats-clear event: %s",
                   strerror (errno));
        ERRNO_SAFE_WRAP (free, topic);
        return -1;
    }
    free (topic);
    return 0;
}

int flux_module_register_handlers (flux_t *h, flux_error_t *error)
{
    struct module_ctx *ctx;
    const char *name;

    if (!h || !(name = flux_aux_get (h, "flux::name"))) {
        errno = EINVAL;
        return errprintf (error, "invalid argument");
    }
    if (!(ctx = calloc (1, sizeof (*ctx)))
        || flux_aux_set (h, NULL, ctx, (flux_free_f)module_ctx_destroy) < 0) {
        module_ctx_destroy (ctx);
        return -1;
    }
    if (!(ctx->w_prepare = register_prepare (h, error))
        || subscribe_events (h, name, error) < 0)
        goto error;
    if (flux_register_default_methods (h, name, &ctx->handlers_default) < 0) {
        errprintf (error,
                   "error registering default service methods: %s",
                   strerror (errno));
        goto error;
    }
    if (flux_msg_handler_addvec_ex (h, name, htab, NULL, &ctx->handlers) < 0) {
        errprintf (error,
                   "error registering additional default service methods: %s",
                   strerror (errno));
        goto error;
    }
    return 0;
error:
    return -1;
}

/* Respond with ENOSYS to any requests still in the module's message queue
 * after transitioning to FINALIZING, which ensures the broker won't add
 * any new ones.
 */
static void respond_to_unhandled (flux_t *h)
{
    flux_msg_t *msg;

    while ((msg = flux_recv (h, FLUX_MATCH_REQUEST, FLUX_O_NONBLOCK))) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        flux_log (h, LOG_DEBUG, "responding to post-shutdown %s", topic);
        if (flux_respond_error (h, msg, ENOSYS, NULL) < 0)
            flux_log_error (h, "responding to post-shutdown %s", topic);
        flux_msg_destroy (msg);
    }
}

int flux_module_finalize (flux_t *h, int errnum, flux_error_t *error)
{
    if (!h) {
        errno = EINVAL;
        return errprintf (error, "invalid argument");
    }
    if (module_set_finalizing (h, 10., error) < 0)
        return -1;
    respond_to_unhandled (h);
    if (module_set_exited (h, errnum, error) < 0)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
