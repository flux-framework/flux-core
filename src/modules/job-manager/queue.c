/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queue.c - job queues
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job-manager.h"
#include "conf.h"
#include "restart.h"
#include "queue.h"

struct jobq {
    char *name;
    bool enable;    // jobs may be submitted to this queue
    char *reason;   // reason if disabled
};

struct queue {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    union {
        struct jobq *anon;
        zhashx_t *named;
    };
    bool have_named_queues;
};

static void jobq_destroy (struct jobq *q)
{
    if (q) {
        int saved_errno = errno;
        free (q->name);
        free (q->reason);
        free (q);
        errno = saved_errno;
    }
}

// zhashx_destructor_fn signature
static void jobq_destructor (void **item)
{
    if (item) {
        jobq_destroy (*item);
        *item = NULL;
    }
}

static struct jobq *jobq_create (const char *name)
{
    struct jobq *q;

    if (!(q = calloc (1, sizeof (*q))))
        return NULL;
    if (name && !(q->name = strdup (name)))
        goto error;
    q->enable = true;
    return q;
error:
    jobq_destroy (q);
    return NULL;
}

static int jobq_enable (struct jobq *q, bool enable, const char *reason)
{
    if (enable) {
        q->enable = true;
        free (q->reason);
        q->reason = NULL;
    }
    else {
        char *cpy;
        if (!(cpy = strdup (reason)))
            return -1;
        free (q->reason);
        q->reason = cpy;
        q->enable = false;
    }
    return 0;
}

static int queue_enable_all (struct queue *queue,
                             bool enable,
                             const char *reason)
{
    if (queue->have_named_queues) {
        struct jobq *q = zhashx_first (queue->named);
        while (q) {
            if (jobq_enable (q, enable, reason) < 0)
                return -1;
            q = zhashx_next (queue->named);
        }
    }
    else {
        if (jobq_enable (queue->anon, enable, reason) < 0)
            return -1;
    }
    return 0;
}

struct jobq *queue_lookup (struct queue *queue,
                           const char *name,
                           flux_error_t *error)
{
    if (name) {
        struct jobq *q;

        if (!queue->have_named_queues
            || !(q = zhashx_lookup (queue->named, name))) {
            errprintf (error, "'%s' is not a valid queue", name);
            return NULL;
        }
        return q;
    }
    else {
        if (queue->have_named_queues) {
            errprintf (error, "a named queue is required");
            return NULL;
        }
        return queue->anon;
    }
}

static int queue_save_jobq_append (json_t *a, struct jobq *q)
{
    json_t *entry;
    if (!q->name)
        entry = json_pack ("{s:b}", "enable", q->enable);
    else
        entry = json_pack ("{s:s s:b}", "name", q->name, "enable", q->enable);
    if (!entry)
        goto nomem;
    if (!q->enable) {
        json_t *o = json_string (q->reason);
        if (!o)
            goto nomem;
        if (json_object_set_new (entry, "reason", o) < 0) {
            json_decref (o);
            goto nomem;
        }
    }
    if (json_array_append_new (a, entry) < 0)
        goto nomem;
    return 0;
nomem:
    json_decref (entry);
    errno = ENOMEM;
    return -1;
}

json_t *queue_save_state (struct queue *queue)
{
    json_t *a;
    struct jobq *q;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (queue->have_named_queues) {
        q = zhashx_first (queue->named);
        while (q) {
            if (queue_save_jobq_append (a, q) < 0)
                goto error;
            q = zhashx_next (queue->named);
        }
    }
    else {
        if (queue_save_jobq_append (a, queue->anon) < 0)
            goto error;
    }
    return a;
error:
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

int queue_restore_state (struct queue *queue, int version, json_t *o)
{
    size_t index;
    json_t *entry;

    if (version != 0 || !o || !json_is_array (o)) {
        errno = EINVAL;
        return -1;
    }
    json_array_foreach (o, index, entry) {
        const char *name = NULL;
        const char *reason = NULL;
        int enable;
        struct jobq *q = NULL;

        if (json_unpack (entry,
                         "{s?s s:b s?s}",
                         "name", &name,
                         "enable", &enable,
                         "reason", &reason) < 0) {
            errno = EINVAL;
            return -1;
        }
        if (name && queue->have_named_queues)
            q = zhashx_lookup (queue->named, name);
        else if (!name && !queue->have_named_queues)
            q = queue->anon;
        if (q) {
            if (jobq_enable (q, enable, reason) < 0)
                return -1;
        }
    }
    return 0;
}

int queue_submit_check (struct queue *queue,
                        json_t *jobspec,
                        flux_error_t *error)
{
    struct jobq *q;
    json_t *o;
    const char *name = NULL;

    if ((o = jpath_get (jobspec, "attributes.system.queue")))
        name = json_string_value (o);

    if (!(q = queue_lookup (queue, name, error))) {
        errno = EINVAL;
        return -1;
    }
    if (!q->enable) {
        errprintf (error, "job submission%s%s is disabled: %s",
                   name ? " to " : "",
                   name ? name : "",
                   q->reason);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* N.B. the broker will have already validated the basic queue configuration so
 * we shouldn't need to produce detailed configuration errors for users here.
 */
static int queue_configure (const flux_conf_t *conf,
                            flux_error_t *error,
                            void *arg)
{
    struct queue *queue = arg;
    json_t *queues;

    if (flux_conf_unpack (conf, NULL, "{s:o}", "queues", &queues) == 0
        && json_object_size (queues) > 0) {
        const char *name;
        json_t *value;
        struct jobq *q;
        zlistx_t *keys;

        /* destroy anon queue and create hash if necessary
         */
        if (!queue->have_named_queues) {
            queue->have_named_queues = true;
            jobq_destroy (queue->anon);
            if (!(queue->named = zhashx_new ()))
                goto nomem;
            zhashx_set_destructor (queue->named, jobq_destructor);
        }
        /* remove any queues that disappeared from config
         */
        if (!(keys = zhashx_keys (queue->named)))
            goto nomem;
        name = zlistx_first (keys);
        while (name) {
            if (!json_object_get (queues, name))
                zhashx_delete (queue->named, name);
            name = zlistx_next (keys);
        }
        zlistx_destroy (&keys);
        /* add any new queues that appeared in config
         */
        json_object_foreach (queues, name, value) {
            if (!zhashx_lookup (queue->named, name)) {
                if (!(q = jobq_create (name))
                    || zhashx_insert (queue->named, name, q) < 0) {
                    jobq_destroy (q);
                    goto nomem;
                }
            }
        }
    }
    else {
        if (queue->have_named_queues) {
            queue->have_named_queues = false;
            zhashx_destroy (&queue->named);
            if (!(queue->anon = jobq_create (NULL)))
                goto nomem;
        }
    }
    return 1;
nomem:
    errprintf (error, "out of memory while processing queue configuration");
    errno = ENOMEM;
    return -1;
}

static void queue_list_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct queue *queue = arg;
    struct jobq *q;
    json_t *a = NULL;;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    if (queue->have_named_queues) {
        q = zhashx_first (queue->named);
        while (q) {
            json_t *o;
            if (!(o = json_string (q->name))
                || json_array_append_new (a, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
            q = zhashx_next (queue->named);
        }
    }
    if (flux_respond_pack (h, msg, "{s:O}", "queues", a) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (a);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (a);
}

static void queue_status_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct queue *queue = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    struct jobq *q;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s?s}", "name", &name) < 0)
        goto error;
    if (!(q = queue_lookup (queue, name, &error))) {
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (q->enable)
        rc = flux_respond_pack (h, msg, "{s:b}", "enable", 1);
    else {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:b s:s}",
                                "enable", 0,
                                "reason", q->reason);
    }
    if (rc < 0)
        flux_log_error (h, "error responding to job-manager.queue-status");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-status");
}

static void queue_admin_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct queue *queue = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    int enable;
    const char *reason = NULL;
    int all;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:b s?s s:b}",
                             "name", &name,
                             "enable", &enable,
                             "reason", &reason,
                             "all", &all) < 0)
        goto error;
    if (!enable && !reason) {
        errmsg = "reason is required for disable";
        errno = EINVAL;
        goto error;
    }
    if (!name) {
        if (queue->have_named_queues && !all) {
            errmsg = "Use --all to apply this command to all queues";
            errno = EINVAL;
            goto error;
        }
        if (queue_enable_all (queue, enable, reason))
            goto error;
    }
    else {
        struct jobq *q;
        if (!(q = queue_lookup (queue, name, &error))) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
        if (jobq_enable (q, enable, reason) < 0)
            goto error;
    }
    if (restart_save_state (queue->ctx) < 0)
        flux_log_error (h, "problem saving checkpoint after queue change");
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-admin");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-admin");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-list",
        queue_list_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-status",
        queue_status_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-admin",
        queue_admin_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void queue_destroy (struct queue *queue)
{
    if (queue) {
        int saved_errno = errno;
        conf_unregister_callback (queue->ctx->conf, queue_configure);
        flux_msg_handler_delvec (queue->handlers);
        if (queue->have_named_queues)
            zhashx_destroy (&queue->named);
        else
            jobq_destroy (queue->anon);
        free (queue);
        errno = saved_errno;
    }
}

struct queue *queue_create (struct job_manager *ctx)
{
    struct queue *queue;
    flux_error_t error;

    if (!(queue = calloc (1, sizeof (*queue))))
        return NULL;
    queue->ctx = ctx;
    if (!(queue->anon = jobq_create (NULL)))
        goto error;
    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 queue,
                                 &queue->handlers) < 0)
        goto error;
    if (conf_register_callback (ctx->conf,
                                &error,
                                queue_configure,
                                queue) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "error parsing queue config: %s",
                  error.text);
        goto error;
    }
    return queue;
error:
    queue_destroy (queue);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
