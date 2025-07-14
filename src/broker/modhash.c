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
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "module.h"
#include "broker.h"
#include "modhash.h"
#include "overlay.h"

struct modhash {
    zhash_t *zh_byuuid;
    flux_msg_handler_t **handlers;
    struct broker *ctx;
};

static json_t *modhash_get_modlist (modhash_t *mh,
                                    double now,
                                    struct service_switch *sw);

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
    return module_sendmsg_new (p, msg);
}

static void modhash_add (modhash_t *mh, module_t *p)
{
    /* always succeeds - uuids are by definition unique */
    (void)zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    zhash_freefn (mh->zh_byuuid,
                  module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
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
    const flux_msg_t *msg = module_pop_insmod (p);

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

    flux_msg_decref (msg);
    return rc;
}

static int module_rmmod_respond (flux_t *h, module_t *p)
{
    const flux_msg_t *msg;
    int rc = 0;
    while ((msg = module_pop_rmmod (p))) {
        if (flux_respond (h, msg, NULL) < 0)
            rc = -1;
        flux_msg_decref (msg);
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

        if (module_insmod_respond (ctx->h, p) < 0)
            flux_log_error (ctx->h, "flux_respond to insmod %s", name);

        if (module_rmmod_respond (ctx->h, p) < 0)
            flux_log_error (ctx->h, "flux_respond to rmmod %s", name);

        modhash_remove (ctx->modhash, p);
    }
}

static int mod_svc_cb (flux_msg_t **msg, void *arg)
{
    module_t *p = arg;
    return module_sendmsg_new (p, msg);
}

/* Load broker module.
 * 'name' is the name to use for the module (NULL = use dso basename minus ext)
 * 'path' is either a dso path or a dso basename (e.g. "kvs" or "/a/b/kvs.so".
 */
int modhash_load (modhash_t *mh,
                  const char *name,
                  const char *path,
                  json_t *args,
                  const flux_msg_t *request,
                  flux_error_t *error)
{
    const char *searchpath;
    char *pattern = NULL;
    zlist_t *files = NULL;
    module_t *p;

    if (!strchr (path, '/')) {
        if (!(searchpath = getenv ("FLUX_MODULE_PATH"))) {
            errprintf (error, "FLUX_MODULE_PATH is not set in the environment");
            errno = EINVAL;
            return -1;
        }
        if (asprintf (&pattern, "%s.so*", path) < 0) {
            errprintf (error, "out of memory");
            return -1;
        }
        if (!(files = dirwalk_find (searchpath,
                                    DIRWALK_REALPATH | DIRWALK_NORECURSE,
                                    pattern,
                                    1,
                                    NULL,
                                    NULL))
            || zlist_size (files) == 0) {
            errprintf (error, "module not found in search path");
            errno = ENOENT;
            goto error;
        }
        path = zlist_first (files);
    }
    if (!(p = module_create (mh->ctx->h,
                             overlay_get_uuid (mh->ctx->overlay),
                             name,
                             path,
                             mh->ctx->rank,
                             args,
                             error)))
        goto error;
    modhash_add (mh, p);
    if (service_add (mh->ctx->services,
                     module_get_name (p),
                     module_get_uuid (p),
                     mod_svc_cb,
                     p) < 0) {
        errprintf (error, "error registering %s service", module_get_name (p));
        goto module_remove;
    }
    module_set_poller_cb (p, module_cb, mh->ctx);
    module_set_status_cb (p, module_status_cb, mh->ctx);
    if (request && module_push_insmod (p, request) < 0) { // response deferred
        errprintf (error, "error saving %s request", module_get_name (p));
        goto service_remove;
    }
    if (module_start (p) < 0) {
        errprintf (error, "error starting %s module", module_get_name (p));
        goto service_remove;
    }
    flux_log (mh->ctx->h, LOG_DEBUG, "insmod %s", module_get_name (p));
    zlist_destroy (&files);
    free (pattern);
    return 0;
service_remove:
    service_remove_byuuid (mh->ctx->services, module_get_uuid (p));
module_remove:
    modhash_remove (mh, p);
error:
    ERRNO_SAFE_WRAP (zlist_destroy, &files);
    ERRNO_SAFE_WRAP (free, pattern);
    return -1;
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

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:s s:o}",
                             "name", &name,
                             "path", &path,
                             "args", &args) < 0)
        goto error;
    if (modhash_load (ctx->modhash, name, path, args, msg, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}


static int unload_module (broker_ctx_t *ctx,
                          const char *name,
                          bool cancel,
                          const flux_msg_t *request)
{
    module_t *p;

    if (!(p = modhash_lookup_byname (ctx->modhash, name))) {
        errno = ENOENT;
        return -1;
    }
    if (cancel) {
        flux_error_t error;
        if (module_cancel (p, &error) < 0) {
            log_msg ("%s: %s", name, error.text);
            return -1;
        }
    }
    else {
        if (module_stop (p, ctx->h) < 0)
            return -1;
    }
    if (module_push_rmmod (p, request) < 0)
        return -1;
    flux_log (ctx->h, LOG_DEBUG, "rmmod %s", name);
    return 0;
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

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?b}",
                             "name", &name,
                             "cancel", &cancel) < 0)
        goto error;
    if (unload_module (ctx, name, cancel, msg) < 0)
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
    json_t *names = NULL;
    size_t index;
    json_t *entry;
    const char *errmsg = NULL;
    flux_error_t error;
    zlist_t *l = NULL;
    module_t *p;

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
    /* Put modules in a list as the names are checked,
     */
    if (!(l = zlist_new ()))
        goto nomem;
    json_array_foreach (names, index, entry) {
        const char *name = json_string_value (entry);
        if (!(p = modhash_lookup_byname (ctx->modhash, (name)))) {
            errprintf (&error, "%s module is not loaded", name);
            errmsg = error.text;
            errno = ENOENT;
            goto error;
        }
        if (zlist_append (l, p) < 0)
            goto nomem;
    }
    p = zlist_first (l);
    while (p) {
        (void)module_trace (p, msg);
        p = zlist_next (l);
    }
    zlist_destroy (&l);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to module.trace");
    zlist_destroy (&l);
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
    module_t *p;

    p = zhash_first (ctx->modhash->zh_byuuid);
    while (p) {
        module_trace_disconnect (p, msg);
        p = zhash_next (ctx->modhash->zh_byuuid);
    }
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
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &mh->handlers) < 0)
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
                       "path", module_get_path (p),
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
    zlist_t *uuids;
    char *uuid;
    module_t *result = NULL;

    if (!name)
        return NULL;
    if (!(uuids = zhash_keys (mh->zh_byuuid))) {
        errno = ENOMEM;
        return NULL;
    }
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
        if (p) {
            if (streq (module_get_name (p), name)
                || streq (module_get_path (p), name)) {
                result = p;
                break;
            }
        }
        uuid = zlist_next (uuids);
    }
    zlist_destroy (&uuids);
    return result;
}

int modhash_event_mcast (modhash_t *mh, const flux_msg_t *msg)
{
    module_t *p;

    p = zhash_first (mh->zh_byuuid);
    while (p) {
        if (module_event_cast (p, msg) < 0)
            return -1;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
