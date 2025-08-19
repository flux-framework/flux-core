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
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "module.h"
#include "module_dso.h"
#include "broker.h"
#include "trace.h"
#include "modhash.h"
#include "overlay.h"

struct modhash {
    zhash_t *zh_byuuid;
    flux_msg_handler_t **handlers;
    struct broker *ctx;
    struct flux_msglist *trace_requests;
    flux_future_t *f_builtins_load;
    flux_future_t *f_builtins_unload;
};

extern struct module_builtin builtin_connector_local;
extern struct module_builtin builtin_barrier;
extern struct module_builtin builtin_heartbeat;

/* Builtin modules with autoload=true are loaded in this order and
 * unloaded in the reverse order.
 */
static struct module_builtin *builtins[] = {
    &builtin_connector_local,
    &builtin_barrier,
    &builtin_heartbeat,
};

static json_t *modhash_get_modlist (modhash_t *mh,
                                    double now,
                                    struct service_switch *sw);
static void modhash_load_builtins_cond_fulfill (modhash_t *mh,
                                                flux_future_t *f);
static void modhash_unload_builtins_cond_fulfill (modhash_t *mh,
                                                  flux_future_t *f);
static module_t *modhash_load_builtin (modhash_t *mh,
                                      struct module_builtin *bb,
                                      const char *name,
                                      json_t *args,
                                      flux_error_t *error);

int modhash_response_sendmsg_new (modhash_t *mh, flux_msg_t **msg)
{
    const char *uuid;
    module_t *p;

    if (!*msg)
        return 0;
    if (!(uuid = flux_msg_route_last (*msg))) {
        errno = EPROTO;
        return -1;
    }
    if (!(p = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOSYS;
        return -1;
    }
    trace_module_msg (mh->ctx->h,
                      "rx",
                      module_get_name (p),
                      mh->trace_requests,
                      *msg);
    return module_sendmsg_new (p, msg);
}

static int modhash_add (modhash_t *mh, module_t *p)
{
    if (module_aux_set (p, "modhash", mh, NULL) < 0)
        return -1;
    /* always succeeds - uuids are by definition unique */
    (void)zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    zhash_freefn (mh->zh_byuuid,
                  module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
    return 0;
}

static void modhash_remove (modhash_t *mh, module_t *p)
{
    zhash_delete (mh->zh_byuuid, module_get_uuid (p));
}

static int module_insmod_respond (flux_t *h, module_t *p)
{
    int rc;
    int errnum = 0;
    int status = module_get_status (p);
    const flux_msg_t *msg = module_aux_get (p, "insmod");

    if (msg == NULL)
        return 0;

    /* If the module is EXITED, return error to insmod if mod_main() < 0
     */
    if (status == FLUX_MODSTATE_EXITED)
        errnum = module_get_errnum (p);
    if (errnum == 0)
        rc = flux_respond (h, msg, NULL);
    else
        rc = flux_respond_error (h, msg, errnum, NULL);

    module_aux_set (p, "insmod", NULL, NULL);
    return rc;
}

static int module_rmmod_respond (flux_t *h, module_t *p)
{
    struct flux_msglist *requests = module_aux_get (p, "rmmod");
    const flux_msg_t *msg;
    int rc = 0;

    if (requests) {
        while ((msg = flux_msglist_pop (requests))) {
            if (flux_respond (h, msg, NULL) < 0)
                rc = -1;
            flux_msg_decref (msg);
        }
    }
    return rc;
}

/* If a message from a connector-routed client is not matched by this function,
 * then it will fail with EAGAIN if the broker is in a pre-INIT state.
 */
static bool allow_early_request (const flux_msg_t *msg)
{
    const struct flux_match match[] = {
        // state-machine.wait may be needed early by flux_reconnect(3) users
        { FLUX_MSGTYPE_REQUEST, FLUX_MATCHTAG_NONE, "state-machine.wait" },
        // let state-machine.get and attr.get work for flux-uptime(1)
        { FLUX_MSGTYPE_REQUEST, FLUX_MATCHTAG_NONE, "state-machine.get" },
        { FLUX_MSGTYPE_REQUEST, FLUX_MATCHTAG_NONE, "attr.get" },
        { FLUX_MSGTYPE_REQUEST, FLUX_MATCHTAG_NONE, "log.dmesg" },
    };
    for (int i = 0; i < ARRAY_SIZE (match); i++)
        if (flux_msg_cmp (msg, match[i]))
            return true;
    return false;
}

/* Callback to send disconnect messages on behalf of unloading module.
 */
static void disconnect_send_cb (const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    flux_msg_t *cpy;
    if (!(cpy = flux_msg_copy (msg, false)))
        return;
    broker_request_sendmsg_new (ctx, &cpy);
}

/* Handle messages on the service socket of a module.
 */
static void module_cb (module_t *p, void *arg)
{
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg = module_recvmsg (p);
    int type;
    int count;

    if (!msg)
        goto done;
    trace_module_msg (ctx->h,
                      "tx",
                      module_get_name (p),
                      ctx->modhash->trace_requests,
                      msg);
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            (void)broker_response_sendmsg_new (ctx, &msg);
            break;
        case FLUX_MSGTYPE_REQUEST:
            count = flux_msg_route_count (msg);
            /* Requests originated by the broker module will have a route
             * count of 1.  Ensure that, when the module is unloaded, a
             * disconnect message is sent to all services used by broker module.
             */
            if (count == 1) {
                if (module_disconnect_arm (p, msg, disconnect_send_cb, ctx) < 0)
                    flux_log_error (ctx->h, "error arming module disconnect");
            }
            /* Requests sent by the module on behalf of _its_ peers, e.g.
             * connector-local module with connected clients, will have a
             * route count greater than one here.  If this broker is not
             * "online" (entered INIT state), politely rebuff these requests.
             * Possible scenario for this message: user submitting a job on
             * a login node before cluster reboot is complete.
             */
            else if (count > 1 && !ctx->online && !allow_early_request (msg)) {
                const char *errmsg = "Upstream Flux broker is offline."
                                     " Try again later.";

                if (flux_respond_error (ctx->h, msg, EAGAIN, errmsg) < 0)
                    flux_log_error (ctx->h, "send offline response message");
                break;
            }
            broker_request_sendmsg_new (ctx, &msg);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (broker_event_sendmsg_new (ctx, &msg) < 0) {
                flux_log_error (ctx->h,
                                "%s(%s): broker_event_sendmsg_new %s",
                                __FUNCTION__,
                                module_get_name (p),
                                flux_msg_typestr (type));
            }
            break;
        default:
            flux_log (ctx->h,
                      LOG_ERR,
                      "%s(%s): unexpected %s",
                      __FUNCTION__,
                      module_get_name (p),
                      flux_msg_typestr (type));
            break;
    }
done:
    flux_msg_destroy (msg);
}

static void module_status_cb (module_t *p, int prev_status, void *arg)
{
    broker_ctx_t *ctx = arg;
    int status = module_get_status (p);
    const char *name = module_get_name (p);

    modhash_load_builtins_cond_fulfill (ctx->modhash,
                                        ctx->modhash->f_builtins_load);

    /* Transition from INIT
     * If module started normally, i.e. INIT->RUNNING, then
     * respond to insmod requests now. O/w, delay responses until
     * EXITED, when any errnum is available.
     */
    if (prev_status == FLUX_MODSTATE_INIT
        && status == FLUX_MODSTATE_RUNNING) {
        if (module_insmod_respond (ctx->h, p) < 0)
            flux_log_error (ctx->h, "flux_respond to insmod %s", name);
    }

    /* Transition to EXITED
     * Remove service routes, respond to insmod & rmmod request(s), if any,
     * and remove the module (which calls pthread_join).
     */
    if (status == FLUX_MODSTATE_EXITED) {
        flux_log (ctx->h, LOG_DEBUG, "module %s exited", name);
        service_remove_byuuid (ctx->services, module_get_uuid (p));

        if (!module_unload_requested (p)
            && !module_aux_get (p, "insmod")
            && module_get_errnum (p) != 0) {
            bool nopanic = false;
            const char *val = NULL;
            (void)attr_get (ctx->attrs, "broker.module-nopanic", &val, NULL);
            if (val && !streq (val, "0"))
                nopanic = true;

            if (nopanic)
                flux_log (ctx->h, LOG_CRIT, "%s module runtime failure", name);
            else
                broker_panic (ctx, "%s module runtime failure", name);
        }

        if (module_insmod_respond (ctx->h, p) < 0)
            flux_log_error (ctx->h, "flux_respond to insmod %s", name);

        if (module_rmmod_respond (ctx->h, p) < 0)
            flux_log_error (ctx->h, "flux_respond to rmmod %s", name);

        modhash_remove (ctx->modhash, p);

        modhash_unload_builtins_cond_fulfill (ctx->modhash,
                                              ctx->modhash->f_builtins_unload);
    }
}

static int mod_svc_cb (flux_msg_t **msg, void *arg)
{
    module_t *p = arg;
    modhash_t *mh = module_aux_get (p, "modhash");

    if (mh) {
        trace_module_msg (mh->ctx->h,
                          "rx",
                          module_get_name (p),
                          mh->trace_requests,
                          *msg);
    }
    return module_sendmsg_new (p, msg);
}

/* Perform the final steps of loading a broker module:
 * - set status and message callbacks
 * - register a service under the module name
 * - start the module thread
 * * insert the module object into the modhash
 */
static int modhash_load_finalize (struct modhash *mh,
                                  module_t *p,
                                  flux_error_t *error)
{
    module_set_poller_cb (p, module_cb, mh->ctx);
    module_set_status_cb (p, module_status_cb, mh->ctx);
    if (service_add (mh->ctx->services,
                     module_get_name (p),
                     module_get_uuid (p),
                     mod_svc_cb,
                     p) < 0) {
        errprintf (error, "error registering %s service", module_get_name (p));
        return -1;
    }
    if (module_start (p) < 0) {
        errprintf (error, "error starting %s module", module_get_name (p));
        return -1;
    }
    modhash_add (mh, p);

    return 0;
}

static module_t *modhash_load_dso (modhash_t *mh,
                                   const char *name_or_null,
                                   const char *path_or_name,
                                   json_t *args,
                                   flux_error_t *error)
{
    broker_ctx_t *ctx = mh->ctx;
    const char *broker_uuid = overlay_get_uuid (ctx->overlay);
    char *path = NULL;
    char *name = NULL;
    void *dso;
    mod_main_f mod_main;
    module_t *p;

    /* Handle 'flux module load /path/to/foo.dso' or 'flux module load foo'.
     * In the latter case, search FLUX_MODULE_PATH for foo.dso.
     */
    if (strchr (path_or_name, '/')) {
        if (!(path = strdup (path_or_name))) {
            errprintf (error, "error duplicating module path");
            return NULL;
        }
    }
    else {
        const char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath) {
            errprintf (error, "FLUX_MODULE_PATH is not set in the environment");
            errno = EINVAL;
            return NULL;
        }
        if (!(path = module_dso_search (path_or_name, searchpath, error)))
            return NULL;
    }
    /* If the name is not specified, derive it from the module path.
     * E.g. the path '/path/to/foo.dso' suggests a module name of 'foo'.
     * This doesn't have to be true if the name is specified.
     */
    if (name_or_null)
        name = strdup (name_or_null);
    else
        name = module_dso_name (path);
    if (!name) {
        errprintf (error,
                   "error duplicating module name: %s",
                   strerror (errno));
        ERRNO_SAFE_WRAP (free, path);
        goto error;
    }
    /* Now open the DSO and obtain the mod_main() function pointer
     * that will be called from a new module thread in module_start().
     * The name is only passed to this function so the deprecated mod_name
     * symbol can be sanity checked, if defined.
     */
    if (!(dso = module_dso_open (path, name, &mod_main, error))) {
        ERRNO_SAFE_WRAP (free, path);
        goto error;
    }
    /* Create the module object.
     */
    if (!(p = module_create (ctx->h,
                             broker_uuid,
                             name,
                             mod_main,
                             args,
                             error))
        || module_aux_set (p, NULL, dso, module_dso_close) < 0) {
        module_dso_close (dso);
        ERRNO_SAFE_WRAP (free, path);
        goto error_module;
    }
    if (module_aux_set (p, "path", path, (flux_free_f)free) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        goto error_module;
    }
    /*  Register service, start module thread, and insert into modhash.
     */
    if (modhash_load_finalize (ctx->modhash, p, error) < 0)
        goto error_module;
    flux_log (ctx->h, LOG_DEBUG, "insmod %s", module_get_name (p));
    free (name);
    return p;
error_module:
    module_destroy (p);
error:
    ERRNO_SAFE_WRAP (free, name);
    return NULL;
}

struct module_builtin *builtins_find (modhash_t *mh, const char *name)
{
    for (int i = 0; i < ARRAY_SIZE (builtins); i++) {
        if (streq (name, builtins[i]->name))
            return builtins[i];
    }
    return NULL;
}

/* Load a module, asynchronously.
 * N.B. modhash_load () handles response, unless it returns -1.
 */
static void load_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name = NULL;
    const char *path;
    json_t *args;
    flux_error_t error;
    const char *errmsg = NULL;
    module_t *p;
    struct module_builtin *builtin;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:s s:o}",
                             "name", &name,
                             "path", &path,
                             "args", &args) < 0)
        goto error;
    if ((builtin = builtins_find (ctx->modhash, path))) {
        p = modhash_load_builtin (ctx->modhash, builtin, name, args, &error);
        if (!p) {
            errmsg = error.text;
            goto error;
        }
    }
    else {
        if (!(p = modhash_load_dso (ctx->modhash, name, path, args, &error))) {
            errmsg = error.text;
            goto error;
        }
    }
    /* Push the insmod request onto the module.  A response will be generated
     * from the module status callback, after the module is active.
     */
    if (module_aux_set (p,
                        "insmod",
                        (flux_msg_t *)msg,
                        (flux_free_f)flux_msg_decref) < 0) {
        errprintf (&error, "error saving %s request", module_get_name (p));
        errmsg = error.text;
        goto error_module;
    }
    flux_msg_incref (msg);
    return;
error_module:
    module_destroy (p);
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static module_t *unload_module (broker_ctx_t *ctx,
                                const char *name,
                                bool cancel)
{
    module_t *p;

    if (!(p = modhash_lookup_byname (ctx->modhash, name))) {
        errno = ENOENT;
        return NULL;
    }
    if (cancel) {
        flux_error_t error;
        if (module_cancel (p, &error) < 0) {
            log_msg ("%s: %s", name, error.text);
            return NULL;
        }
    }
    else {
        if (module_stop (p, ctx->h) < 0)
            return NULL;
    }
    flux_log (ctx->h, LOG_DEBUG, "rmmod %s", name);
    return p;
}

/* Unload a module, asynchronously.
 * N.B. unload_module() handles response, unless it fails early
 * and returns -1.
 */
static void remove_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;
    int cancel = 0;
    module_t *p;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?b}",
                             "name", &name,
                             "cancel", &cancel) < 0)
        goto error;
    if (!(p = unload_module (ctx, name, cancel)))
        goto error;
    struct flux_msglist *requests = module_aux_get (p, "rmmod");
    if (!requests) {
        if (!(requests = flux_msglist_create ())
            || module_aux_set (p,
                               "rmmod",
                               requests,
                               (flux_free_f)flux_msglist_destroy) < 0) {
            flux_msglist_destroy (requests);
            goto error;
        }
    }
    if (flux_msglist_push (requests, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* List loaded modules
 */
static void list_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    broker_ctx_t *ctx = arg;
    json_t *mods = NULL;
    double now = flux_reactor_now (flux_get_reactor (h));

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(mods = modhash_get_modlist (ctx->modhash, now, ctx->services)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:O}", "mods", mods) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (mods);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void debug_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;
    int defer = -1;
    module_t *p;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?b}",
                             "name", &name,
                             "defer", &defer) < 0)
        goto error;
    if (!(p = modhash_lookup_byname (ctx->modhash, name))) {
        errno = ENOENT;
        goto error;
    }
    if (defer != -1) {
        if (module_set_defer (p, defer) < 0)
            goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to module.debug request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to module.debug request");
}

static void trace_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    broker_ctx_t *ctx = arg;
    struct flux_match match = FLUX_MATCH_ANY;
    json_t *names;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:i s:s}",
                             "names", &names,
                             "typemask", &match.typemask,
                             "topic_glob", &match.topic_glob) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg) || !json_is_array (names)) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msglist_append (ctx->modhash->trace_requests, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to module.trace");
}

static void status_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    broker_ctx_t *ctx = arg;
    int status;
    int errnum = 0;
    const char *sender;
    module_t *p;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s?i}",
                             "status", &status,
                             "errnum", &errnum) < 0
        || !(sender = flux_msg_route_first (msg))
        || !(p = modhash_lookup (ctx->modhash, sender))) {
        const char *errmsg = "error decoding/finding module.status";
        if (flux_msg_is_noresponse (msg))
            flux_log_error (h, "%s", errmsg);
        else if (flux_respond_error (h, msg, errno, errmsg) < 0)
            flux_log_error (h, "error responding to module.status");
        return;
    }
    switch (status) {
        case FLUX_MODSTATE_FINALIZING:
            module_mute (p);
            break;
        case FLUX_MODSTATE_EXITED:
            module_set_errnum (p, errnum);
            break;
        default:
            break;
    }
    /* Send a response if required.
     * Hint: module waits for response in FINALIZING state.
     */
    if (!flux_msg_is_noresponse (msg)) {
        if (flux_respond (h, msg, NULL) < 0) {
            flux_log_error (h,
                            "%s: error responding to module.status",
                            module_get_name (p));
        }
    }
    /* N.B. this will cause module_status_cb() to be called.
     */
    module_set_status (p, status);
}

static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    broker_ctx_t *ctx = arg;

    (void)flux_msglist_disconnect (ctx->modhash->trace_requests, msg);
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "module.load",
        load_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.remove",
        remove_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.list",
        list_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.status",
        status_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.debug",
        debug_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.trace",
        trace_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "module.disconnect",
        disconnect_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

modhash_t *modhash_create (struct broker *ctx)
{
    modhash_t *mh = calloc (1, sizeof (*mh));
    if (!mh)
        return NULL;
    mh->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &mh->handlers) < 0
        || !(mh->trace_requests = flux_msglist_create ()))
        goto error;
    if (!(mh->zh_byuuid = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    return mh;
error:
    modhash_destroy (mh);
    return NULL;
}

int modhash_destroy (modhash_t *mh)
{
    int saved_errno = errno;
    const char *uuid;
    module_t *p;
    int count = 0;

    if (mh) {
        if (mh->zh_byuuid) {
            FOREACH_ZHASH (mh->zh_byuuid, uuid, p) {
                log_msg ("broker module '%s' was not properly shut down",
                         module_get_name (p));
                flux_error_t error;
                if (module_cancel (p, &error) < 0)
                    log_msg ("%s: %s", module_get_name (p), error.text);
                count++;
            }
            zhash_destroy (&mh->zh_byuuid);
        }
        flux_msg_handler_delvec (mh->handlers);
        flux_msglist_destroy (mh->trace_requests);
        flux_future_destroy (mh->f_builtins_load);
        flux_future_destroy (mh->f_builtins_unload);
        free (mh);
    }
    errno = saved_errno;
    return count;
}

static json_t *modhash_entry_tojson (module_t *p,
                                     double now,
                                     struct service_switch *sw)
{
    json_t *svcs;
    json_t *entry = NULL;

    if (!(svcs  = service_list_byuuid (sw, module_get_uuid (p))))
        return NULL;
    entry = json_pack ("{s:s s:s s:i s:i s:O s:i s:i}",
                       "name", module_get_name (p),
                       "path", module_aux_get (p, "path"),
                       "idle", (int)(now - module_get_lastseen (p)),
                       "status", module_get_status (p),
                       "services", svcs,
                       "sendqueue", module_get_send_queue_count (p),
                       "recvqueue", module_get_recv_queue_count (p));
    json_decref (svcs);
    return entry;
}

/* Prepare RFC 5 'mods' array for lsmod response.
 */
static json_t *modhash_get_modlist (modhash_t *mh,
                                    double now,
                                    struct service_switch *sw)
{
    json_t *mods = NULL;
    module_t *p;

    if (!(mods = json_array()))
        goto nomem;
    p = zhash_first (mh->zh_byuuid);
    while (p) {
        json_t *entry;

        if (!(entry = modhash_entry_tojson (p, now, sw))
            || json_array_append_new (mods, entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        p = zhash_next (mh->zh_byuuid);
    }
    return mods;
nomem:
    json_decref (mods);
    errno = ENOMEM;
    return NULL;
}

module_t *modhash_lookup (modhash_t *mh, const char *uuid)
{
    module_t *m;

    if (!(m = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOENT;
        return NULL;
    }
    return m;
}

module_t *modhash_lookup_byname (modhash_t *mh, const char *name)
{
    if (name) {
        module_t *p = zhash_first (mh->zh_byuuid);
        while (p) {
            if (streq (module_get_name (p), name)
                || streq (module_aux_get (p, "path"), name))
                return p;
            p = zhash_next (mh->zh_byuuid);
        }
    }
    return NULL;
}

int modhash_event_mcast (modhash_t *mh, const flux_msg_t *msg)
{
    const char *topic;
    module_t *p;

    if (flux_msg_get_topic (msg, &topic) < 0)
        return -1;
    p = zhash_first (mh->zh_byuuid);
    while (p) {
        if (module_is_subscribed (p, topic)) {
            trace_module_msg (mh->ctx->h,
                              "rx",
                              module_get_name (p),
                              mh->trace_requests,
                              msg);
            flux_msg_t *cpy;
            if (!(cpy = flux_msg_copy (msg, true))
                || module_sendmsg_new (p, &cpy) < 0) {
                flux_msg_decref (cpy);
                return -1;
            }
        }
        p = zhash_next (mh->zh_byuuid);
    }
    return 0;
}

module_t *modhash_first (modhash_t *mh)
{
    return zhash_first (mh->zh_byuuid);
}

module_t *modhash_next (modhash_t *mh)
{
    return zhash_next (mh->zh_byuuid);
}

int modhash_service_add (modhash_t *mh,
                         const char *sender,
                         const char *name,
                         flux_error_t *error)
{
    struct broker *ctx = mh->ctx;
    module_t *p;

    if (!(p = modhash_lookup (mh, sender))) {
        errprintf (error, "requestor is not local");
        errno = ENOENT;
        return -1;
    }
    if (service_add (ctx->services, name, sender, mod_svc_cb, p) < 0) {
        errprintf (error,
                   "could not register service %s for module %s: %s",
                   name,
                   module_get_name (p),
                   strerror (errno));
        return -1;
    }
    return 0;
}

int modhash_service_remove (modhash_t *mh,
                            const char *sender,
                            const char *name,
                            flux_error_t *error)
{
    struct broker *ctx = mh->ctx;
    const char *uuid;

    if (!(uuid = service_get_uuid (ctx->services, name))) {
        errprintf (error, "%s is not registered", name);
        errno = ENOENT;
        return -1;
    }
    if (!streq (uuid, sender)) {
        errprintf (error, "requestor did not register %s", name);
        errno = EINVAL;
        return -1;
    }
    service_remove (ctx->services, name);
    return 0;
}

static module_t *modhash_load_builtin (modhash_t *mh,
                                       struct module_builtin *bb,
                                       const char *name,
                                       json_t *args,
                                       flux_error_t *error)
{
    const char *broker_uuid = overlay_get_uuid (mh->ctx->overlay);
    module_t *p;
    char *cpy;

    if (!(p = module_create (mh->ctx->h,
                             broker_uuid,
                             name ? name : bb->name,
                             bb->main,
                             args,
                             error)))
        return NULL;
    if (!(cpy = strdup ("builtin"))
        || module_aux_set (p, "path", cpy, (flux_free_f)free) < 0) {
        errprintf (error,
                   "error duplicating module path: %s",
                   strerror (errno));
        ERRNO_SAFE_WRAP (free, cpy);
        goto error;
    }
    if (modhash_load_finalize (mh, p, error) < 0)
        goto error;
    return p;
error:
    module_destroy (p);
    return NULL;
}

static void modhash_load_builtins_cond_fulfill (modhash_t *mh,
                                                flux_future_t *f)
{
    int waiting = 0;
    flux_error_t error;

    if (!f || flux_future_is_ready (f))
        return;
    for (int i = 0; i < ARRAY_SIZE (builtins); i++) {
        if (builtins[i]->autoload) {
            module_t *p;

            if (!(p = modhash_lookup_byname (mh, builtins[i]->name))) {
                errprintf (&error,
                           "%s is unexpectedly missing from the module hash",
                           builtins[i]->name);
                goto error;
            }
            switch (module_get_status (p)) {
                case FLUX_MODSTATE_INIT:
                    waiting++;
                    break;
                case FLUX_MODSTATE_RUNNING:
                    break;
                case FLUX_MODSTATE_FINALIZING:
                    errprintf (&error,
                               "%s is unexpectedly finalizing",
                               module_get_name (p));
                    goto error;
                case FLUX_MODSTATE_EXITED:
                    errprintf (&error,
                               "%s has unexpectedly exited: %s",
                               module_get_name (p),
                               strerror (module_get_errnum (p)));
                    goto error;
            }
        }
    }
    if (waiting == 0)
        flux_future_fulfill (f, NULL, NULL);
    return;
error:
     flux_future_fatal_error (f, EINVAL, error.text);
}

flux_future_t *modhash_load_builtins (modhash_t *mh, flux_error_t *error)
{
    if (!mh->f_builtins_load) {
        flux_future_t *f;
        if (!(f = flux_future_create (NULL, NULL))) {
            errprintf (error, "could not create future");
            return NULL;
        }
        flux_future_set_reactor (f, flux_get_reactor (mh->ctx->h));
        mh->f_builtins_load = f;
    }
    for (int i = 0; i < ARRAY_SIZE (builtins); i++) {
        if (builtins[i]->autoload) {
            if (mh->ctx->verbose > 1)
                log_msg ("loading %s", builtins[i]->name);
            if (!modhash_load_builtin (mh, builtins[i], NULL, NULL, error))
                return NULL;
        }
    }
    modhash_load_builtins_cond_fulfill (mh, mh->f_builtins_load);
    return mh->f_builtins_load;
}

/* Fulfill the future if it is pending and no builtin modules are loaded.
 */
static void modhash_unload_builtins_cond_fulfill (modhash_t *mh,
                                                  flux_future_t *f)
{
    if (!f || flux_future_is_ready (f))
        return;
    for (int i = 0; i < ARRAY_SIZE (builtins); i++) {
        if (builtins[i]->autoload) {
            if (modhash_lookup_byname (mh, builtins[i]->name) != NULL)
                return;
        }
    }
    flux_future_fulfill (f, NULL, NULL);
}

flux_future_t *modhash_unload_builtins (modhash_t *mh)
{
    if (!mh->f_builtins_unload) {
        flux_future_t *f;
        if (!(f = flux_future_create (NULL, NULL)))
            return NULL;
        flux_future_set_reactor (f, flux_get_reactor (mh->ctx->h));
        mh->f_builtins_unload = f;
    }
    // unload in reverse order
    for (int i = ARRAY_SIZE (builtins); i > 0; i--) {
        struct module_builtin *mod = builtins[i - 1];

        if (mod->autoload) {
            if (mh->ctx->verbose > 1)
                log_msg ("unloading %s", mod->name);
            if (!unload_module (mh->ctx, mod->name, false)) {
                if (errno != ENOENT) {
                    flux_log_error (mh->ctx->h,
                                    "Warning: error unloading %s",
                                    mod->name);
                }
            }
        }
    }
    modhash_unload_builtins_cond_fulfill (mh, mh->f_builtins_unload);
    return mh->f_builtins_unload;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
