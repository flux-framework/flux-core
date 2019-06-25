/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* PMI-1 service to job
 *
 * Provide PMI-1 service so that an MPI or Flux job can bootstrap.
 * Much of the work is done by the PMI-1 wire protocol engine in
 * libpmi/simple_server.c and libsubprocess socketpair channels.
 *
 * In task.c, shell_task_pmi_enable() sets up the subprocess channel,
 * sets the PMI_FD, PMI_RANK, and PMI_SIZE environment variables, and
 * arranges for a callback to be invoked when the client has sent
 * a PMI request on the channel.  The shell mainline registers
 * shell_pmi_task_ready(), below, as this callback.
 *
 * shell_pmi_task_ready() reads the request from the channel and pushes
 * it into the PMI-1 protocol engine.  If the request can be immediately
 * answered, the shell_pmi_response_send() callback registered with the engine
 * is invoked, which writes the response to the subprocess channel.
 *
 * Other requests have callbacks from the engine to provide data,
 * which is fed back to the engine, which then calls shell_pmi_response_send().
 * These are kvs_get, kvs_put, and barrier.  Although the protocol engine
 * is effectively blocked while these callbacks are handled, they are
 * implemented with asynchronous continuation callbacks so that the shell's
 * reactor remains live while they are waiting for an answer.
 *
 * The PMI KVS supports a put / barrier / get pattern.  The barrier
 * distributes KVS data that was "put" so that it is available to "get".
 * A local hash captures key-value pairs as they are put.  If the entire
 * job runs under one shell, the barrier is a no-op, and the gets are
 * serviced only from the cache.  Otherwise, the barrier dumps the hash
 * into a Flux KVS txn and commits it with a flux_kvs_fence(), using
 * the number of shells as "nprocs".  Gets are serviced from the cache,
 * with fall-through to a flux_kvs_lookup().
 *
 * If info->verbose is true (shell --verbose flag was provided), the
 * protocol engine emits client and server telemetry to stderr, and
 * shell_pmi_task_ready() logs read errors, EOF, and finalization to stderr
 * in a compatible format.
 *
 * Caveats:
 * - PMI kvsname parameter is ignored
 * - 64-bit Flux job id's are assigned to integer-typed PMI appnum
 * - PMI publish, unpublish, lookup, spawn are not implemented
 * - Although multiple cycles of put / barrier / get are supported, the
 *   the barrier rewrites data from previous cycles to the Flux KVS.
 * - PMI_Abort() is implemented as log message + exit in the client code.
 *   It does not reach this module.
 * - Teardown of the subprocess channel is deferred until task completion,
 *   although client closes its end after PMI_Finalize().
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <czmq.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libpmi/simple_server.h"
#include "src/common/libutil/log.h"

#include "task.h"
#include "info.h"
#include "pmi.h"

#define FQ_KVS_KEY_MAX (SIMPLE_KVS_KEY_MAX + 128)
#define KVSNAME "pmi"

struct shell_pmi {
    flux_t *h;
    struct shell_info *info;
    struct pmi_simple_server *server;
    zhashx_t *kvs;
    int cycle;      // count cycles of put / barrier / get
};

static int shell_pmi_kvs_put (void *arg,
                              const char *kvsname,
                              const char *key,
                              const char *val)
{
    struct shell_pmi *pmi = arg;

    zhashx_update (pmi->kvs, key, (char *)val);
    return 0;
}

/* Handle kvs lookup response.
 */
static void kvs_lookup_continuation (flux_future_t *f, void *arg)
{
    struct shell_pmi *pmi = arg;
    void *cli = flux_future_aux_get (f, "flux::shell_pmi");
    const char *val = NULL;

    flux_kvs_lookup_get (f, &val); // val remains NULL on failure
    pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
    flux_future_destroy (f);
}

/* Construct a PMI key in job's guest namespace.
 */
static int shell_pmi_kvs_key (char *buf,
                              int bufsz,
                              flux_jobid_t id,
                              const char *key)
{
    char tmp[FQ_KVS_KEY_MAX];

    if (snprintf (tmp, sizeof (tmp), "%s.%s", KVSNAME, key) >= sizeof (tmp))
        return -1;
    return flux_job_kvs_guest_key (buf, bufsz, id, tmp);
}

/* Lookup a key: first try the local hash.   If that fails and the
 * job spans multiple shells, do a KVS lookup in the job's private
 * KVS namespace and handle the response in kvs_lookup_continuation().
 */
static int shell_pmi_kvs_get (void *arg,
                              void *cli,
                              const char *kvsname,
                              const char *key)
{
    struct shell_pmi *pmi = arg;
    const char *val = NULL;

    if ((val = zhashx_lookup (pmi->kvs, key))) {
        pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
        return 0;
    }
    if (pmi->info->shell_size > 1) {
        char nkey[FQ_KVS_KEY_MAX];
        flux_future_t *f = NULL;

        if (shell_pmi_kvs_key (nkey,
                               sizeof (nkey),
                               pmi->info->jobid,
                               key) < 0) {
            log_err ("shell_pmi_kvs_key");
            goto out;
        }
        if (!(f = flux_kvs_lookup (pmi->h, NULL, 0, nkey))) {
            log_err ("flux_kvs_lookup");
            goto out;
        }
        if (flux_future_aux_set (f, "flux::shell_pmi", cli, NULL) < 0) {
            log_err ("flux_future_aux_set");
            flux_future_destroy (f);
            goto out;
        }
        if (flux_future_then (f, -1., kvs_lookup_continuation, pmi) < 0) {
            log_err ("flux_future_then");
            flux_future_destroy (f);
            goto out;
        }
        return 0; // response deferred
    }
out:
    return -1; // cause PMI_KVS_Get() to fail with INVALID_KEY
}

static void kvs_fence_continuation (flux_future_t *f, void *arg)
{
    struct shell_pmi *pmi = arg;
    int rc;

    rc = flux_future_get (f, NULL);
    pmi_simple_server_barrier_complete (pmi->server, rc);
    flux_future_destroy (f);
}

static int shell_pmi_barrier_enter (void *arg)
{
    struct shell_pmi *pmi = arg;
    flux_kvs_txn_t *txn = NULL;
    const char *key;
    const char *val;
    char name[64];
    int nprocs = pmi->info->shell_size;
    flux_future_t *f;
    char nkey[FQ_KVS_KEY_MAX];

    if (pmi->info->shell_size == 1) { // all local: no further sync needed
        pmi_simple_server_barrier_complete (pmi->server, 0);
        return 0;
    }
    snprintf (name, sizeof (name), "pmi.%ju.%d",
             (uintmax_t)pmi->info->jobid,
             pmi->cycle++);
    if (!(txn = flux_kvs_txn_create ())) {
        log_err ("flux_kvs_txn_create");
        goto error;
    }
    val = zhashx_first (pmi->kvs);
    while (val) {
        key = zhashx_cursor (pmi->kvs);
        if (shell_pmi_kvs_key (nkey,
                               sizeof (nkey),
                               pmi->info->jobid,
                               key) < 0) {
            log_err ("key buffer overflow");
            goto error;
        }
        if (flux_kvs_txn_put (txn, 0, nkey, val) < 0) {
            log_err ("flux_kvs_txn_put");
            goto error;
        }
        val = zhashx_next (pmi->kvs);
    }
    if (!(f = flux_kvs_fence (pmi->h, NULL, 0, name, nprocs, txn))) {
        log_err ("flux_kvs_fence");
        goto error;
    }
    if (flux_future_then (f, -1., kvs_fence_continuation, pmi) < 0) {
        log_err ("flux_future_then");
        flux_future_destroy (f);
        goto error;
    }
    flux_kvs_txn_destroy (txn);
    return 0;
error:
    flux_kvs_txn_destroy (txn);
    return -1; // cause PMI_Barrier() to fail
}

static int shell_pmi_response_send (void *client, const char *buf)
{
    struct shell_task *task = client;

    return shell_task_pmi_write (task, buf, strlen (buf));
}

static void shell_pmi_debug_trace (void *client, const char *line)
{
    struct shell_task *task = client;

    fprintf (stderr, "%d: %s", task->rank, line);
}

// shell_task_pmi_ready_f callback footprint
void shell_pmi_task_ready (struct shell_task *task, void *arg)
{
    struct shell_pmi *pmi = arg;
    int len;
    const char *line;
    int rc;

    len = shell_task_pmi_readline (task, &line);
    if (len < 0) {
        if (pmi->info->verbose)
            fprintf (stderr, "%d: C: pmi read error: %s\n",
                     task->rank, flux_strerror (errno));
        return;
    }
    if (len == 0) {
        if (pmi->info->verbose)
            fprintf (stderr, "%d: C: pmi EOF\n", task->rank);
        return;
    }
    rc = pmi_simple_server_request (pmi->server, line, task, task->rank);
    if (rc < 0) {
        if (pmi->info->verbose)
            fprintf (stderr, "%d: S: pmi request error\n", task->rank);
        return;
    }
    if (rc == 1) {
        if (pmi->info->verbose)
            fprintf (stderr, "%d: S: pmi finalized\n", task->rank);
    }
}

void shell_pmi_destroy (struct shell_pmi *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        pmi_simple_server_destroy (pmi->server);
        zhashx_destroy (&pmi->kvs);
        free (pmi);
        errno = saved_errno;
    }
}

// zhashx_duplicator_fn footprint
static void *kvs_value_duplicator (const void *item)
{
    void *cpy = NULL;
    if (item)
        cpy = strdup (item);
    return cpy;
}

// zhashx_destructor_fn footprint
static void kvs_value_destructor (void **item)
{
    if (*item) {
        free (*item);
        *item = NULL;
    }
}

static struct pmi_simple_ops shell_pmi_ops = {
    .kvs_put        = shell_pmi_kvs_put,
    .kvs_get        = shell_pmi_kvs_get,
    .barrier_enter  = shell_pmi_barrier_enter,
    .response_send  = shell_pmi_response_send,
    .debug_trace    = shell_pmi_debug_trace,
};

struct shell_pmi *shell_pmi_create (flux_t *h, struct shell_info *info)
{
    struct shell_pmi *pmi;
    int flags = info->verbose ? PMI_SIMPLE_SERVER_TRACE : 0;

    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    pmi->h = h;
    pmi->info = info;
    if (!(pmi->server = pmi_simple_server_create (shell_pmi_ops,
                                                  info->jobid,
                                                  info->jobspec->task_count,
                                                  info->rankinfo.ntasks,
                                                  "pmi",
                                                  flags,
                                                  pmi)))
        goto error;
    if (!(pmi->kvs = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (pmi->kvs, kvs_value_destructor);
    zhashx_set_duplicator (pmi->kvs, kvs_value_duplicator);
    return pmi;
error:
    shell_pmi_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
