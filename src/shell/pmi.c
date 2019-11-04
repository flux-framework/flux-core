/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* builtin PMI-1 plugin for jobs
 *
 * Provide PMI-1 service so that an MPI or Flux job can bootstrap.
 * Much of the work is done by the PMI-1 wire protocol engine in
 * libpmi/simple_server.c and libsubprocess socketpair channels.
 *
 * At startup this module is registered as a builtin shell plugin under
 * the name "pmi"via an entry in builtins.c builtins array.
 *
 * At shell "init", the plugin intiailizes a PMI object including the
 * pmi simple server and empty local kvs cache.
 *
 * During each task's "task init" callback, the pmi plugin sets up the
 * subprocess channel, sets the PMI_FD, PMI_RANK, and PMI_SIZE environment
 * variables, and subscribes to the newly created PMI_FD channel in order
 * to read PMI requests.
 *
 * The output callback pmi_fd_read_cb() reads the request from the PMI_FD
 * channel and pushes it into the PMI-1 protocol engine.  If the request
 * can be immediately answered, the shell_pmi_response_send() callback
 * registered with the engine is invoked, which writes the response to
 * the subprocess channel.
 *
 * Other requests have callbacks from the engine to provide data,
 * which is fed back to the engine, which then calls shell_pmi_response_send().
 * These are kvs_get, kvs_put, and barrier.  Although the task
 * is effectively blocked while these callbacks are handled, they are
 * implemented with asynchronous continuation callbacks so that other tasks
 * and the shell's reactor remain live while the task awaits an answer.
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
 * If shell->verbose is true (shell --verbose flag was provided), the
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
#include "src/common/libpmi/clique.h"

#include "builtins.h"
#include "internal.h"
#include "task.h"

#define FQ_KVS_KEY_MAX (SIMPLE_KVS_KEY_MAX + 128)

struct shell_pmi {
    flux_shell_t *shell;
    struct pmi_simple_server *server;
    zhashx_t *kvs;
    zhashx_t *locals;
    int cycle;      // count cycles of put / barrier / get
};

static void shell_pmi_abort (void *arg,
                             void *client,
                             int exit_code,
                             const char *msg)
{
    /* Generate job exception (exit_code ignored for now) */
    shell_die (exit_code,
               "MPI_Abort%s%s",
               msg ? ": " : "",
               msg ? msg : "");
}

static int shell_pmi_kvs_put (void *arg,
                              const char *kvsname,
                              const char *key,
                              const char *val)
{
    struct shell_pmi *pmi = arg;

    zhashx_update (pmi->kvs, key, (char *)val);
    return 0;
}

static void pmi_kvs_put_local (struct shell_pmi *pmi,
                               const char *key,
                               const char *val)
{
    zhashx_update (pmi->kvs, key, (char *)val);
    zhashx_update (pmi->locals, key, (void *) 0x1);
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
 * Put it in a subdir named "pmi".
 */
static int shell_pmi_kvs_key (char *buf,
                              int bufsz,
                              flux_jobid_t id,
                              const char *key)
{
    char tmp[FQ_KVS_KEY_MAX];

    if (snprintf (tmp, sizeof (tmp), "pmi.%s", key) >= sizeof (tmp))
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
    flux_t *h = pmi->shell->h;
    const char *val = NULL;

    if ((val = zhashx_lookup (pmi->kvs, key))) {
        pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
        return 0;
    }
    if (pmi->shell->info->shell_size > 1) {
        char nkey[FQ_KVS_KEY_MAX];
        flux_future_t *f = NULL;

        if (shell_pmi_kvs_key (nkey,
                               sizeof (nkey),
                               pmi->shell->jobid,
                               key) < 0) {
            shell_log_errno ("shell_pmi_kvs_key");
            goto out;
        }
        if (!(f = flux_kvs_lookup (h, NULL, 0, nkey))) {
            shell_log_errno ("flux_kvs_lookup");
            goto out;
        }
        if (flux_future_aux_set (f, "flux::shell_pmi", cli, NULL) < 0) {
            shell_log_errno ("flux_future_aux_set");
            flux_future_destroy (f);
            goto out;
        }
        if (flux_future_then (f, -1., kvs_lookup_continuation, pmi) < 0) {
            shell_log_errno ("flux_future_then");
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
    int nprocs = pmi->shell->info->shell_size;
    flux_future_t *f;
    char nkey[FQ_KVS_KEY_MAX];

    if (nprocs == 1) { // all local: no further sync needed
        pmi_simple_server_barrier_complete (pmi->server, 0);
        return 0;
    }
    snprintf (name, sizeof (name), "pmi.%ju.%d",
             (uintmax_t)pmi->shell->jobid,
             pmi->cycle++);
    if (!(txn = flux_kvs_txn_create ())) {
        shell_log_errno ("flux_kvs_txn_create");
        goto error;
    }
    val = zhashx_first (pmi->kvs);
    while (val) {
        key = zhashx_cursor (pmi->kvs);
        /* Special case:
         * Keys in pmi->locals are not added to the KVS transaction
         * because they were locally generated and need not be
         * shared with the other shells.
         */
        if (zhashx_lookup (pmi->locals, key)) {
            val = zhashx_next (pmi->kvs);
            continue;
        }
        if (shell_pmi_kvs_key (nkey,
                               sizeof (nkey),
                               pmi->shell->jobid,
                               key) < 0) {
            shell_log_errno ("key buffer overflow");
            goto error;
        }
        if (flux_kvs_txn_put (txn, 0, nkey, val) < 0) {
            shell_log_errno ("flux_kvs_txn_put");
            goto error;
        }
        val = zhashx_next (pmi->kvs);
    }
    if (!(f = flux_kvs_fence (pmi->shell->h, NULL, 0, name, nprocs, txn))) {
        shell_log_errno ("flux_kvs_fence");
        goto error;
    }
    if (flux_future_then (f, -1., kvs_fence_continuation, pmi) < 0) {
        shell_log_errno ("flux_future_then");
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

    return flux_subprocess_write (task->proc, "PMI_FD", buf, strlen (buf));
}

static void shell_pmi_debug_trace (void *client, const char *line)
{
    struct shell_task *task = client;

    shell_trace ("%d: %s", task->rank, line);
}

static void pmi_fd_cb (flux_shell_task_t *task,
                       const char *stream,
                       void *arg)
{
    struct shell_pmi *pmi = arg;
    int len;
    const char *line;
    int rc;

    line = flux_subprocess_read_line (task->proc, "PMI_FD", &len);
    if (len < 0) {
        shell_trace ("%d: C: pmi read error: %s",
                     task->rank, flux_strerror (errno));
        return;
    }
    if (len == 0) {
        shell_trace ("%d: C: pmi EOF", task->rank);
        return;
    }
    rc = pmi_simple_server_request (pmi->server, line, task, task->rank);
    if (rc < 0) {
        shell_trace ("%d: S: pmi request error", task->rank);
        return;
    }
    if (rc == 1) {
        shell_trace ("%d: S: pmi finalized", task->rank);
    }
}

/* Generate 'PMI_process_mapping' key (see RFC 13) for MPI clique computation.
 *
 * Create an array of pmi_map_block structures, sized for worst case mapping
 * (no compression possible).  Walk through the rcalc info for each shell rank.
 * If shell's mapping looks identical to previous one, increment block->nodes;
 * otherwise consume another array slot.  Finally, encode to string, put it
 * in the local KVS hash, and free array.
 */
static int init_clique (struct shell_pmi *pmi)
{
    struct pmi_map_block *blocks;
    int nblocks;
    int i;
    char val[SIMPLE_KVS_VAL_MAX];

    if (!(blocks = calloc (pmi->shell->info->shell_size, sizeof (*blocks))))
        return -1;
    nblocks = 0;

    for (i = 0; i < pmi->shell->info->shell_size; i++) {
        struct rcalc_rankinfo ri;

        if (rcalc_get_nth (pmi->shell->info->rcalc, i, &ri) < 0)
            goto error;
        if (nblocks == 0 || blocks[nblocks - 1].procs != ri.ntasks) {
            blocks[nblocks].nodeid = i;
            blocks[nblocks].procs = ri.ntasks;
            blocks[nblocks].nodes = 1;
            nblocks++;
        }
        else
            blocks[nblocks - 1].nodes++;
    }
    /* If value exceeds SIMPLE_KVS_VAL_MAX, skip setting the key
     * without generating an error.  The client side will not treat
     * a missing key as an error.  It should be unusual though so log it.
     */
    if (pmi_process_mapping_encode (blocks, nblocks, val, sizeof (val)) < 0) {
        shell_log_errno ("pmi_process_mapping_encode");
        goto out;
    }
    pmi_kvs_put_local (pmi, "PMI_process_mapping", val);
out:
    free (blocks);
    return 0;
error:
    free (blocks);
    errno = EINVAL;
    return -1;
}

static int set_flux_instance_level (struct shell_pmi *pmi)
{
    char *p;
    long l;
    int n;
    int rc = -1;
    char val [SIMPLE_KVS_VAL_MAX];
    const char *level = flux_attr_get (pmi->shell->h, "instance-level");

    if (!level)
        return 0;

    errno = 0;
    l = strtol (level, &p, 10);
    if (errno != 0 || *p != '\0' || l < 0) {
        shell_log_error ("set_flux_instance_level level=%s invalid", level);
        goto out;
    }
    n = snprintf (val, sizeof (val), "%lu", l+1);
    if (n >= sizeof (val)) {
        shell_log_errno ("set_flux_instance_level: snprintf");
        goto out;
    }
    pmi_kvs_put_local (pmi, "flux.instance-level", val);
    rc = 0;
out:
    return rc;
}

static void pmi_destroy (struct shell_pmi *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        pmi_simple_server_destroy (pmi->server);
        zhashx_destroy (&pmi->kvs);
        zhashx_destroy (&pmi->locals);
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
    .abort          = shell_pmi_abort,
};


static struct shell_pmi *pmi_create (flux_shell_t *shell)
{
    struct shell_pmi *pmi;
    struct shell_info *info = shell->info;
    int flags = shell->verbose ? PMI_SIMPLE_SERVER_TRACE : 0;
    char kvsname[32];

    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    pmi->shell = shell;
    snprintf (kvsname, sizeof (kvsname), "%ju", (uintmax_t)shell->jobid);
    if (!(pmi->server = pmi_simple_server_create (shell_pmi_ops,
                                                  0, // appnum
                                                  info->jobspec->task_count,
                                                  info->rankinfo.ntasks,
                                                  kvsname,
                                                  flags,
                                                  pmi)))
        goto error;
    if (!(pmi->kvs = zhashx_new ())
        || !(pmi->locals = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (pmi->kvs, kvs_value_destructor);
    zhashx_set_duplicator (pmi->kvs, kvs_value_duplicator);
    if (init_clique (pmi) < 0)
        goto error;
    if (!shell->standalone) {
        if (set_flux_instance_level (pmi) < 0)
            goto error;
    }
    return pmi;
error:
    pmi_destroy (pmi);
    return NULL;
}

static int shell_pmi_init (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *arg,
                           void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_pmi *pmi;
    if (!shell || !(pmi = pmi_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p, "pmi", pmi, (flux_free_f) pmi_destroy) < 0) {
        pmi_destroy (pmi);
        return -1;
    }
    return 0;
}

static int shell_pmi_task_init (flux_plugin_t *p,
                                const char *topic,
                                flux_plugin_arg_t *args,
                                void *arg)
{
    flux_shell_t *shell;
    struct shell_pmi *pmi;
    flux_shell_task_t *task;
    flux_cmd_t *cmd;

    if (!(shell = flux_plugin_get_shell (p))
        || !(pmi = flux_plugin_aux_get (p, "pmi"))
        || !(task = flux_shell_current_task (shell))
        || !(cmd = flux_shell_task_cmd (task)))
        return -1;

    if (flux_cmd_add_channel (cmd, "PMI_FD") < 0)
        return -1;
    if (flux_cmd_setenvf (cmd, 1, "PMI_RANK", "%d", task->rank) < 0)
        return -1;
    if (flux_cmd_setenvf (cmd, 1, "PMI_SIZE", "%d", task->size) < 0)
        return -1;
    if (flux_shell_task_channel_subscribe (task, "PMI_FD", pmi_fd_cb, pmi) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_pmi = {
    .name = "pmi",
    .init = shell_pmi_init,
    .task_init = shell_pmi_task_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
