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
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <signal.h>
#include <pthread.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/aux.h"
#include "ccan/str/str.h"

#include "module.h"
#include "modservice.h"

struct module_ctx {
    flux_t *h;
    bool mod_main_failed;
    int mod_main_errno;
    int argc;
    char **argv;
    size_t argz_len;
    char *argz;
    char *name;
    char *uuid;
};

static void module_thread_cleanup (void *arg);

static int setup_module_profiling (const char *name)
{
    size_t len = strlen (name);
    // one character longer than target to pass -Wstringop-truncation
    char local_name[17] = {0};
    const char *name_ptr = name;
    // pthread name is limited to 16 bytes including \0 on linux
    if (len > 15) {
        strncpy (local_name, name, 16);
        local_name[15] = 0;
        name_ptr = local_name;
    }
    // Set the name of each thread to its module name
#if HAVE_PTHREAD_SETNAME_NP_WITH_TID
    (void) pthread_setname_np (pthread_self (), name_ptr);
#else // e.g. macos
    (void) pthread_setname_np (name_ptr);
#endif
    return (0);
}

static int attr_cache_from_json (flux_t *h, json_t *cache)
{
    const char *name;
    json_t *o;

    json_object_foreach (cache, name, o) {
        const char *val = json_string_value (o);
        if (flux_attr_set_cacheonly (h, name, val) < 0)
            return -1;
    }
    return 0;
}

/* Decode welcome message and
 * - set ctx->name, ctx->uuid
 * - set ctx->argc, ctx->argv
 * - populate the broker attr cache in ctx->h
 */
static int welcome_decode_new (struct module_ctx *ctx, flux_msg_t **msg)
{
    json_t *args;
    json_t *attrs;
    json_t *conf;
    const char *name;
    const char *uuid;

    if (flux_request_unpack (*msg,
                             NULL,
                             "{s:o s:o s:o s:s s:s}",
                             "args", &args,
                             "attrs", &attrs,
                             "conf", &conf,
                             "name", &name,
                             "uuid", &uuid) < 0)
        return -1;

    if (!(ctx->name = strdup (name))
        || !(ctx->uuid = strdup (uuid)))
        goto error;

    if (attr_cache_from_json (ctx->h, attrs) < 0)
        goto error;

    flux_conf_t *cf;
    if (!(cf = flux_conf_pack ("O", conf))
        || flux_set_conf_new (ctx->h, cf) < 0) {
        flux_conf_decref (cf);
        goto error;
    }

    if (!json_is_null (args)) {
        size_t index;
        json_t *entry;

        json_array_foreach (args, index, entry) {
            const char *s = json_string_value (entry);
            if (s && (argz_add (&ctx->argz, &ctx->argz_len, s) != 0)) {
                errno = ENOMEM;
                goto error;
            }
        }
    }
    ctx->argc = argz_count (ctx->argz, ctx->argz_len);
    if (!(ctx->argv = calloc (1, sizeof (ctx->argv[0]) * (ctx->argc + 1))))
        goto error;
    argz_extract (ctx->argz, ctx->argz_len, ctx->argv);

    flux_msg_decref (*msg);
    *msg = NULL;
    return 0;
error:
    return -1;
}

/*  Synchronize the FINALIZING state with the broker, so the broker
 *   can stop messages to this module until we're fully shutdown.
 */
static int module_finalizing (flux_t *h, double timeout)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i}",
                             "status", FLUX_MODSTATE_FINALIZING))
        || flux_future_wait_for (f, timeout) < 0
        || flux_rpc_get (f, NULL)) {
        flux_log_error (h, "module.status FINALIZING error");
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

void *module_thread (void *arg)
{
    struct module_args *args = arg;
    sigset_t signal_set;
    int errnum;
    struct module_ctx ctx;
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .matchtag = FLUX_MATCHTAG_NONE,
        .topic_glob = "welcome",
    };

    memset (&ctx, 0, sizeof (ctx));
    pthread_cleanup_push (module_thread_cleanup, &ctx);

    /* Connect to broker socket, enable logging, register built-in services
     */
    if (!(ctx.h = flux_open (args->uri, 0))) {
        log_err ("flux_open %s", args->uri);
        goto done;
    }

    /* Receive welcome message
     * ctx.name and ctx.uuid may be used after this.
     */
    flux_msg_t *msg;
    if (!(msg = flux_recv (ctx.h, match, 0))
        || welcome_decode_new (&ctx, (flux_msg_t **)&msg) < 0) {
        flux_msg_decref (msg);
        log_err ("welcome failure");
        goto done;
    }

    flux_log_set_appname (ctx.h, ctx.name);
    setup_module_profiling (ctx.name);

    /* Set flux::uuid and flux::name per RFC 5
     */
    if (flux_aux_set (ctx.h, "flux::uuid", (char *)ctx.uuid, NULL) < 0
        || flux_aux_set (ctx.h, "flux::name", ctx.name, NULL) < 0) {
        log_err ("error setting flux:: attributes");
        goto done;
    }

    /* Register services
     */
    if (modservice_register (ctx.h) < 0) {
        log_err ("error registering internal services");
        goto done;
    }

    /* Block all signals
     */
    if (sigfillset (&signal_set) < 0) {
        log_err ("sigfillset");
        goto done;
    }
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0) {
        log_errn (errnum, "pthread_sigmask");
        goto done;
    }

    /* Run the module's main().
     */
    if (args->main (ctx.h, ctx.argc, ctx.argv) < 0) {
        ctx.mod_main_failed = true;
        ctx.mod_main_errno = errno;
    }
done:
    pthread_cleanup_pop (1);

    return NULL;
}

/* This function is invoked in the module thread context in one of two ways:
 * - module_thread() calls pthread_cleanup_pop(3) upon return of mod_main()
 * - pthread_cancel(3) terminates the module thread at a cancellation point
 * pthread_cancel(3) can be called in two situations:
 * - flux module remove --cancel
 * - when modhash_destroy() is called with lingering modules
 * Since modhash_destroy() is called after exiting the broker reactor loop,
 * the broker won't be responsive to any RPCs from this module thread.
 */
static void module_thread_cleanup (void *arg)
{
    struct module_ctx *ctx = arg;
    flux_msg_t *msg;
    flux_future_t *f;

    if (ctx->mod_main_failed) {
        if (ctx->mod_main_errno == 0)
            ctx->mod_main_errno = ECONNRESET;
        flux_log (ctx->h, LOG_CRIT, "module exiting abnormally");
    }

    /* Before processing unhandled requests, ensure that this module
     * is "muted" in the broker. This ensures the broker won't try to
     * feed a message to this module after we've closed the handle,
     * which could cause the broker to block.
     */
    if (module_finalizing (ctx->h, 1.0) < 0)
        flux_log_error (ctx->h, "failed to set module state to finalizing");

    /* If any unhandled requests were received during shutdown,
     * respond to them now with ENOSYS.
     */
    while ((msg = flux_recv (ctx->h, FLUX_MATCH_REQUEST, FLUX_O_NONBLOCK))) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        flux_log (ctx->h, LOG_DEBUG, "responding to post-shutdown %s", topic);
        if (flux_respond_error (ctx->h, msg, ENOSYS, NULL) < 0)
            flux_log_error (ctx->h, "responding to post-shutdown %s", topic);
        flux_msg_destroy (msg);
    }
    if (!(f = flux_rpc_pack (ctx->h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i s:i}",
                             "status", FLUX_MODSTATE_EXITED,
                             "errnum", ctx->mod_main_errno))) {
        flux_log_error (ctx->h, "module.status EXITED error");
        goto done;
    }
    flux_future_destroy (f);
done:
    flux_close (ctx->h);
    free (ctx->argz);
    free (ctx->argv);
    free (ctx->name);
    free (ctx->uuid);
}

// vi:ts=4 sw=4 expandtab
