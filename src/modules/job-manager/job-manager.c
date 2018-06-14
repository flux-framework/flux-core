/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "active.h"
#include "purge.h"
#include "list.h"
#include "priority.h"


struct job_manager_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct queue *queue;
};

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    json_t *jobs;
    size_t index;
    json_t *el;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    json_array_foreach (jobs, index, el) {
        flux_jobid_t id;
        uint32_t userid;
        int priority;
        double t_submit;
        struct job *job = NULL;

        if (json_unpack (el, "{s:I s:i s:i s:f}", "id", &id,
                                                  "priority", &priority,
                                                  "userid", &userid,
                                                  "t_submit", &t_submit) < 0) {
            flux_log (h, LOG_ERR, "%s: error decoding job index %u",
                      __FUNCTION__, (unsigned int)index);
            goto error;
        }
        if (!(job = job_create (id, priority, userid, t_submit, 0)))
            goto error;
        /* N.B. ignore EEXIST, in case restart_from_kvs() loaded a job
         * while its submit request was in still flight.
         */
        if (queue_insert (ctx->queue, job) < 0 && errno != EEXIST) {
            flux_log_error (h, "%s: queue_insert %llu",
                            __FUNCTION__, (unsigned long long)id);
            job_decref (job);
            goto error;
        }
        job_decref (job);
    }
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_log (h, LOG_DEBUG, "%s: added %u jobs", __FUNCTION__,
                            (unsigned int)index);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* list request handled in list.c
 */
static void list_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;

    list_handle_request (h, ctx->queue, msg);
}

/* purge request handled in purge.c
 */
static void purge_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    purge_handle_request (h, ctx->queue, msg);
}

/* priority request handled in priority.c
 */
static void priority_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    priority_handle_request (h, ctx->queue, msg);
}

/* active_map_f callback
 * This is called for each job encountered in the KVS during startup.
 * The 'job' struct is valid only during the callback.
 * queue_insert() increments the 'job' usecount upon successful insert.
 */
static int restart_map_cb (struct job *job, void *arg)
{
    struct job_manager_ctx *ctx = arg;

    if (queue_insert (ctx->queue, job) < 0)
        return -1;
    return 0;
}

/* Load any active jobs present in the KVS at startup.
 */
static int restart_from_kvs (flux_t *h, struct job_manager_ctx *ctx)
{
    int count;

    if ((count = active_map (h, restart_map_cb, ctx)) < 0)
        return -1;
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__, count);
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.submit", submit_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.list", list_cb, FLUX_ROLE_USER},
    { FLUX_MSGTYPE_REQUEST, "job-manager.purge", purge_cb, FLUX_ROLE_USER},
    { FLUX_MSGTYPE_REQUEST, "job-manager.priority", priority_cb, FLUX_ROLE_USER},
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_manager_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;

    if (!(ctx.queue = queue_create ())) {
        flux_log_error (h, "error creating queue");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (restart_from_kvs (h, &ctx) < 0) {
        flux_log_error (h, "restart_from_kvs");
        goto done;
    }
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    queue_destroy (ctx.queue);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
