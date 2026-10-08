/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* conf.c - handle job manager configuration
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libfluxutil/conf_policy.h"

#include "job-manager.h"
#include "journal.h"
#include "conf.h"

struct conf_callback {
    const char *name;
    conf_update_f cb;
    conf_update_f rollback;     // NULL: roll back by replaying to 'cb'
    void *arg;
};

struct conf {
    zlistx_t *callbacks;
    flux_msg_handler_t **handlers;
    struct job_manager *ctx;
};

static void conf_callback_destroy (struct conf_callback *ccb)
{
    if (ccb) {
        int saved_errno = errno;
        free (ccb);
        errno = saved_errno;
    }
}

// zlistx_destructor_fn signature
static void conf_callback_destructor (void **item)
{
    if (item) {
        conf_callback_destroy (*item);
        *item = NULL;
    }
}

static struct conf_callback *conf_callback_create (const char *name,
                                                   conf_update_f cb,
                                                   conf_update_f rollback,
                                                   void *arg)
{
    struct conf_callback *ccb;

    if (!(ccb = calloc (1, sizeof (*ccb))))
        return NULL;
    ccb->name = name;
    ccb->cb = cb;
    ccb->rollback = rollback;
    ccb->arg = arg;
    return ccb;
}

void conf_unregister_callback (struct conf *conf, conf_update_f cb)
{
    struct conf_callback *ccb;

    ccb = zlistx_first (conf->callbacks);
    while (ccb) {
        if (ccb->cb == cb) {
            zlistx_delete (conf->callbacks, zlistx_cursor (conf->callbacks));
            break;
        }
        ccb = zlistx_next (conf->callbacks);
    }
}

int conf_register_callback (struct conf *conf,
                            flux_error_t *error,
                            const char *name,
                            conf_update_f cb,
                            conf_update_f rollback,
                            void *arg)
{
    struct conf_callback *ccb;
    int rc;

    rc = cb (flux_get_conf (conf->ctx->h), error, arg);

    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }
    if (rc == 1) {
        if (!(ccb = conf_callback_create (name, cb, rollback, arg))
            || zlistx_add_end (conf->callbacks, ccb) == NULL) {
            conf_callback_destroy (ccb);
            errprintf (error, "out of memory adding config callback");
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

/* Replay the committed configuration to every callback through 'last'
 * inclusive, or to all of them if 'last' is NULL. The commit is the final
 * step of a reload, so until then flux_get_conf() returns the configuration
 * in effect before this one.
 *
 * 'last' is the callback that rejected the configuration. It is rolled back
 * along with those that accepted, since it may have applied part of the
 * configuration before failing, and it accepted the replayed configuration
 * once already.
 *
 * The callbacks are independent, so the replay runs in registration order,
 * matching the broker's revert_updates() at module scope.
 *
 * A failure here has nowhere to go, so log it naming the registrant and
 * carry on: the remaining callbacks may still roll back cleanly.
 */
static void conf_rollback (struct conf *conf, struct conf_callback *last)
{
    const flux_conf_t *committed = flux_get_conf (conf->ctx->h);
    struct conf_callback *ccb;
    flux_error_t error;

    ccb = zlistx_first (conf->callbacks);
    while (ccb) {
        conf_update_f rollback = ccb->rollback ? ccb->rollback : ccb->cb;

        if (rollback (committed, &error, ccb->arg) < 0)
            flux_log (conf->ctx->h,
                      LOG_ERR,
                      "error rolling back %s config: %s",
                      ccb->name,
                      error.text);
        if (ccb == last)
            break;
        ccb = zlistx_next (conf->callbacks);
    }
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct conf *conf = arg;
    flux_conf_t *instance_conf;
    struct conf_callback *ccb;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_module_config_request_decode (msg, &instance_conf) < 0) {
        errstr = "Error unpacking config-reload request";
        goto error;
    }
    if (conf_policy_validate (instance_conf, &error) < 0) {
        errstr = error.text;
        goto error_decref;
    }
    ccb = zlistx_first (conf->callbacks);
    while (ccb) {
        if (ccb->cb (instance_conf, &error, ccb->arg) < 0) {
            errstr = error.text;
            /* N.B. conf_rollback() reuses the list cursor, so the walk
             * cannot be resumed after this point.
             */
            conf_rollback (conf, ccb);
            errno = EINVAL;
            goto error_decref;
        }
        ccb = zlistx_next (conf->callbacks);
    }
    if (flux_set_conf_new (h, instance_conf) < 0) {
        errstr = "error updating cached configuration";
        conf_rollback (conf, NULL);
        goto error_decref;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error_decref:
    flux_conf_decref (instance_conf);
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.config-reload", config_reload_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void conf_destroy (struct conf *conf)
{
    if (conf) {
        int saved_errno = errno;
        flux_msg_handler_delvec (conf->handlers);
        zlistx_destroy (&conf->callbacks);
        free (conf);
        errno = saved_errno;
    }
}

struct conf *conf_create (struct job_manager *ctx, flux_error_t *error)
{
    struct conf *conf;

    if (!(conf = calloc (1, sizeof (*conf))))
        goto error;
    conf->ctx = ctx;
    if (conf_policy_validate (flux_get_conf (ctx->h), error) < 0)
        goto error_nofill;
    if (!(conf->callbacks = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (conf->callbacks, conf_callback_destructor);
    if (flux_msg_handler_addvec (ctx->h, htab, conf, &conf->handlers) < 0)
        goto error;
    return conf;
error:
    errprintf (error, "%s", strerror (errno));
error_nofill:
    conf_destroy (conf);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
