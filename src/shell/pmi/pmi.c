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
 * the name "pmi" via an entry in builtins.c builtins array.
 *
 * At shell "init", the plugin initializes a PMI object including the
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
 * If shell->verbose is true (shell --verbose flag was provided), the
 * protocol engine emits client and server telemetry to stderr, and
 * shell_pmi_task_ready() logs read errors, EOF, and finalization to stderr
 * in a compatible format.
 *
 * Caveats:
 * - PMI kvsname parameter is ignored
 * - 64-bit Flux job id's are assigned to integer-typed PMI appnum
 * - PMI publish, unpublish, lookup, spawn are not implemented
 * - Teardown of the subprocess channel is deferred until task completion,
 *   although client closes its end after PMI_Finalize().
 */
#define FLUX_SHELL_PLUGIN_NAME "pmi-simple"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "builtins.h"
#include "internal.h"
#include "task.h"
#include "pmi_exchange.h"

struct shell_pmi {
    flux_shell_t *shell;
    struct pmi_simple_server *server;
    json_t *global; // already exchanged
    json_t *pending;// pending to be exchanged
    json_t *locals;  // never exchanged
    struct pmi_exchange *exchange;
};

/* pmi_simple_ops->warn() signature */
static void shell_pmi_warn (void *client, const char *msg)
{
    shell_warn ("%s", msg);
}

/* pmi_simple_ops->abort() signature */
static void shell_pmi_abort (void *arg,
                             void *client,
                             int exit_code,
                             const char *msg)
{
    /*  Attempt to raise job exception and return to the shell's reactor.
     *   This allows the shell to continue to process events and stdio
     *   until the exec system terminates the job due to the exception.
     */
    flux_shell_raise ("exec",
                      0,
                      "PMI_Abort%s%s",
                      msg ? ": " : "",
                      msg ? msg : "");
}

static int put_dict (json_t *dict, const char *key, const char *val)
{
    json_t *o;

    if (!(o = json_string (val)))
        goto nomem;
    if (json_object_set_new (dict, key, o) < 0) {
        json_decref (o);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

/**
 ** ops for using native Flux KVS for PMI KVS
 ** This is used if pmi.kvs=native option is provided.
 **/

static void native_lookup_continuation (flux_future_t *f, void *arg)
{
    struct shell_pmi *pmi = arg;
    void *cli = flux_future_aux_get (f, "pmi_cli");
    const char *val = NULL;

    (void)flux_kvs_lookup_get (f, &val); // leave val=NULL on failure
    pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
    flux_future_destroy (f);
}

static int native_lookup (struct shell_pmi *pmi, const char *key, void *cli)
{
    char *nkey;
    flux_future_t *f;

    if (asprintf (&nkey, "pmi.%s", key) < 0)
        return -1;
    if (!(f = flux_kvs_lookup (pmi->shell->h, NULL, 0, nkey)))
        return -1;
    if (flux_future_aux_set (f, "pmi_cli", cli, NULL) < 0)
        goto error;
    if (flux_future_then (f, -1, native_lookup_continuation, pmi) < 0)
        goto error;
    free (nkey);
    return 0;
error:
    ERRNO_SAFE_WRAP (free, nkey);
    flux_future_destroy (f);
    return -1;
}

static void native_fence_continuation (flux_future_t *f, void *arg)
{
    struct shell_pmi *pmi = arg;
    int rc = flux_future_get (f, NULL);
    pmi_simple_server_barrier_complete (pmi->server, rc);

    flux_future_destroy (f);
    json_object_clear (pmi->pending);
}

static int native_fence (struct shell_pmi *pmi)
{
    flux_kvs_txn_t *txn;
    const char *key;
    json_t *val;
    char *nkey;
    int rc;
    char name[64];
    static int seq = 0;
    uintmax_t id = (uintmax_t)pmi->shell->jobid;
    int size = pmi->shell->info->shell_size;
    flux_future_t *f = NULL;

    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    json_object_foreach (pmi->pending, key, val) {
        if (asprintf (&nkey, "pmi.%s", key) < 0)
            goto error;
        rc = flux_kvs_txn_put (txn, 0, nkey, json_string_value (val));
        ERRNO_SAFE_WRAP (free, nkey);
        if (rc < 0)
            goto error;
    }
    (void)snprintf (name, sizeof (name), "%juPMI%d", id, seq++);
    if (!(f = flux_kvs_fence (pmi->shell->h, NULL, 0, name, size, txn)))
        goto error;
    if (flux_future_then (f, -1, native_fence_continuation, pmi) < 0)
        goto error;
    flux_kvs_txn_destroy (txn);
    return 0;
error:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return -1;
}

/* pmi_simple_ops->kvs_put() signature */
static int native_kvs_put (void *arg,
                           const char *kvsname,
                           const char *key,
                           const char *val)
{
    struct shell_pmi *pmi = arg;

    /* Hack to support "node scope" for partial PMI2 impl needed for Cray.
     */
    if (strstarts (key, "local::"))
        return put_dict (pmi->locals, key, val);
    return put_dict (pmi->pending, key, val);
}

/* pmi_simple_ops->barrier_enter() signature */
static int native_barrier_enter (void *arg)
{
    struct shell_pmi *pmi = arg;

    if (pmi->shell->info->shell_size == 1) {
        pmi_simple_server_barrier_complete (pmi->server, 0);
        return 0;
    }
    if (native_fence (pmi) < 0)
        return -1; // PMI_FAIL
    return 0;
}

/* pmi_simple_ops->kvs_get() signature */
static int native_kvs_get (void *arg,
                           void *cli,
                           const char *kvsname,
                           const char *key)
{
    struct shell_pmi *pmi = arg;
    json_t *o;
    const char *val = NULL;

    if ((o = json_object_get (pmi->locals, key))
            || (o = json_object_get (pmi->pending, key))) {
        val = json_string_value (o);
        pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
        return 0;
    }
    if (pmi->shell->info->shell_size > 1) {
        if (native_lookup (pmi, key, cli) == 0)
            return 0; // response deferred
    }
    return -1; // PMI_ERR_INVALID_KEY
}

/**
 ** ops for using purpose-built dict exchange for PMI KVS
 ** This is used if pmi.kvs=exchange option is provided.
 **/

static void exchange_cb (struct pmi_exchange *pex, void *arg)
{
    struct shell_pmi *pmi = arg;
    int rc = -1;

    if (pmi_exchange_has_error (pex)) {
        shell_warn ("exchange failed");
        goto done;
    }
    if (json_object_update (pmi->global, pmi_exchange_get_dict (pex)) < 0) {
        shell_warn ("failed to update dict after successful exchange");
        goto done;
    }
    json_object_clear (pmi->pending);
    rc = 0;
done:
    pmi_simple_server_barrier_complete (pmi->server, rc);
}

/* pmi_simple_ops->kvs_get() signature */
static int exchange_kvs_get (void *arg,
                             void *cli,
                             const char *kvsname,
                             const char *key)
{
    struct shell_pmi *pmi = arg;
    json_t *o;
    const char *val = NULL;

    if ((o = json_object_get (pmi->locals, key))
        || (o = json_object_get (pmi->pending, key))
        || (o = json_object_get (pmi->global, key))) {
        val = json_string_value (o);
        pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
        return 0;
    }
    return -1; // PMI_ERR_INVALID_KEY
}

/* pmi_simple_ops->barrier_enter() signature */
static int exchange_barrier_enter (void *arg)
{
    struct shell_pmi *pmi = arg;

    if (pmi->shell->info->shell_size == 1) {
        pmi_simple_server_barrier_complete (pmi->server, 0);
        return 0;
    }
    if (pmi_exchange (pmi->exchange,
                      pmi->pending,
                      exchange_cb,
                      pmi) < 0) {
        shell_warn ("pmi_exchange %s", flux_strerror (errno));
        return -1; // PMI_FAIL
    }
    return 0;
}

/* pmi_simple_ops->kvs_put() signature */
static int exchange_kvs_put (void *arg,
                             const char *kvsname,
                             const char *key,
                             const char *val)
{
    struct shell_pmi *pmi = arg;

    /* Hack to support "node scope" for partial PMI2 impl needed for Cray.
     */
    if (strstarts (key, "local::"))
        return put_dict (pmi->locals, key, val);
    return put_dict (pmi->pending, key, val);
}

/**
 ** end of KVS implementations
 **/

/* pmi_simple_ops->response_send() signature */
static int shell_pmi_response_send (void *client, const char *buf)
{
    struct shell_task *task = client;

    return flux_subprocess_write (task->proc, "PMI_FD", buf, strlen (buf));
}

/* pmi_simple_ops->debug_trace() signature */
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

    len = flux_subprocess_read_line (task->proc, "PMI_FD", &line);
    if (len < 0) {
        shell_trace ("%d: C: pmi read error: %s",
                     task->rank,
                     flux_strerror (errno));
        return;
    }
    if (len == 0) {
        shell_trace ("%d: C: pmi EOF", task->rank);
        return;
    }
    rc = pmi_simple_server_request (pmi->server, line, task, task->rank);
    if (rc < 0) {
        shell_trace ("%d: S: pmi request error", task->rank);
        shell_die (1, "PMI-1 wire protocol error");

    }
    if (rc == 1) {
        shell_trace ("%d: S: pmi finalized", task->rank);
    }
}

/* Generate 'PMI_process_mapping' key (see RFC 13) for MPI clique computation.
 *
 * PMI_process_mapping originated with MPICH, which uses it to determine
 * whether it can short circult the comms path between local ranks with shmem.
 * MPICH allows the key to be missing or its value to be empty, and in those
 * cases just skips the optimization.  However, note the following:
 *
 * - MVAPICH2 fails with an "Invalid tag" error in MPI_Init() if the key
 *   does not exist (flux-framework/flux-core#3592) and an even more obscure
 *   error if it exists but is empty
 *
 * - OpenMPI might select conflicting shmem names if the mapping indicates
 *   that ranks are not co-located when they really are
 *   (flux-framework/flux-core#3551)
 */
static int init_clique (struct shell_pmi *pmi)
{
    char *s = NULL;
    if (!(s = taskmap_encode (pmi->shell->info->taskmap, TASKMAP_ENCODE_PMI))
        || strlen (s) > SIMPLE_KVS_VAL_MAX) {
        /* If value exceeds SIMPLE_KVS_VAL_MAX, skip setting the key
         * without generating an error. The client side will not treat
         * a missing key as an error. It should be unusual though so log it.
         */
        if (pmi->shell->info->shell_rank == 0)
            shell_warn ("PMI_process_mapping overflows PMI max value.");
        goto out;
    }
    put_dict (pmi->locals, "PMI_process_mapping", s);
out:
    free (s);
    return 0;
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
    put_dict (pmi->locals, "flux.instance-level", val);
    rc = 0;
out:
    return rc;
}

static int set_flux_taskmap (struct shell_pmi *pmi)
{
    struct taskmap *map = pmi->shell->info->taskmap;
    char *val = NULL;
    int rc = -1;

    if (!(val = taskmap_encode (map, TASKMAP_ENCODE_WRAPPED))
        || strlen (val) > SIMPLE_KVS_VAL_MAX)
        goto out;
    put_dict (pmi->locals, "flux.taskmap", val);
    rc = 0;
out:
    free (val);
    return rc;
}

static void pmi_destroy (struct shell_pmi *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        pmi_simple_server_destroy (pmi->server);
        pmi_exchange_destroy (pmi->exchange);
        json_decref (pmi->global);
        json_decref (pmi->pending);
        json_decref (pmi->locals);
        free (pmi);
        errno = saved_errno;
    }
}

static struct pmi_simple_ops shell_pmi_ops = {
    .response_send  = shell_pmi_response_send,
    .debug_trace    = shell_pmi_debug_trace,
    .abort          = shell_pmi_abort,
    .warn           = shell_pmi_warn,
};

static int parse_args (json_t *config,
                       int *exchange_k,
                       const char **kvs,
                       int *nomap)
{
    json_error_t error;

    if (config) {
        if (json_unpack_ex (config,
                            &error,
                            0,
                            "{s?s s?{s?i !} s?i !}",
                            "kvs", kvs,
                            "exchange",
                              "k", exchange_k,
                            "nomap", nomap) < 0) {
            shell_log_error ("option error: %s", error.text);
            return -1;
        }
    }
    return 0;
}

static struct shell_pmi *pmi_create (flux_shell_t *shell, json_t *config)
{
    struct shell_pmi *pmi;
    struct shell_info *info = shell->info;
    int flags = shell->verbose ? PMI_SIMPLE_SERVER_TRACE : 0;
    char kvsname[32];
    const char *kvs = "exchange";
    int exchange_k = 0; // 0=use default tree fanout
    int nomap = 0;      // avoid generation of PMI_process_mapping

    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    pmi->shell = shell;

    if (parse_args (config, &exchange_k, &kvs, &nomap) < 0)
        goto error;
    if (streq (kvs, "native")) {
        shell_pmi_ops.kvs_put = native_kvs_put;
        shell_pmi_ops.kvs_get = native_kvs_get;
        shell_pmi_ops.barrier_enter = native_barrier_enter;
        if (shell->info->shell_rank == 0)
            shell_warn ("using native Flux kvs implementation");
    }
    else if (streq (kvs, "exchange")) {
        shell_pmi_ops.kvs_put = exchange_kvs_put;
        shell_pmi_ops.kvs_get = exchange_kvs_get;
        shell_pmi_ops.barrier_enter = exchange_barrier_enter;
        if (!(pmi->exchange = pmi_exchange_create (shell, exchange_k)))
            goto error;
    }
    else {
        shell_log_error ("Unknown kvs implementation %s", kvs);
        errno = EINVAL;
        goto error;
    }

    /* Use F58 representation of jobid for "kvsname", since the broker
     * will pull the kvsname and use it as the broker 'jobid' attribute.
     * This allows the broker attribute to be in the "common" user-facing
     * jobid representation.
     */
    if (flux_job_id_encode (shell->jobid,
                            "f58",
                            kvsname,
                            sizeof (kvsname)) < 0)
        goto error;
    if (!(pmi->server = pmi_simple_server_create (shell_pmi_ops,
                                                  0, // appnum
                                                  info->total_ntasks,
                                                  info->rankinfo.ntasks,
                                                  kvsname,
                                                  flags,
                                                  pmi)))
        goto error;
    if (!(pmi->global = json_object ())
        || !(pmi->pending = json_object ())
        || !(pmi->locals = json_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (!nomap && init_clique (pmi) < 0)
        goto error;
    if (set_flux_instance_level (pmi) < 0
        || (!nomap && set_flux_taskmap (pmi) < 0))
        goto error;
    return pmi;
error:
    pmi_destroy (pmi);
    return NULL;
}

static bool member_of_csv (const char *list, const char *name)
{
    char *argz = NULL;
    size_t argz_len;

    if (argz_create_sep (list, ',', &argz, &argz_len) == 0) {
        const char *entry = NULL;

        while ((entry = argz_next (argz, argz_len, entry))) {
            if (streq (entry, name)) {
                free (argz);
                return true;
            }
        }
        free (argz);
    }
    return false;
}

static int shell_pmi_init (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *arg,
                           void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_pmi *pmi;
    json_t *config = NULL;
    const char *pmi_opt = NULL;
    bool enable = false;

    if (flux_shell_getopt_unpack (shell, "pmi", "s", &pmi_opt) < 0) {
        shell_log_error ("pmi shell option must be a string");
        return -1;
    }
    if (flux_shell_getopt_unpack (shell, "pmi-simple", "o", &config) < 0) {
        shell_log_error ("error parsing pmi-simple shell option");
        return -1;
    }
    /* This plugin is disabled _only_ if '-opmi=LIST' was specified without
     * "simple" in LIST.  "pmi1" and "pmi2" are considered aliases for
     * "simple" - see flux-framework/flux-core#5226.
     */
    if (pmi_opt) {
        if (member_of_csv (pmi_opt, "simple"))
            enable = true;
        else if (member_of_csv (pmi_opt, "pmi2")) {
            shell_debug ("pmi2 is interpreted as an alias for simple");
            enable = true;
        }
        else if (member_of_csv (pmi_opt, "pmi1")) {
            shell_debug ("pmi1 is interpreted as an alias for simple");
            enable = true;
        }
    }
    else
        enable = true;
    if (!enable)
        return 0; // plugin disabled
    shell_debug ("simple wire protocol is enabled");

    if (!(pmi = pmi_create (shell, config)))
        return -1;
    if (flux_plugin_aux_set (p, "pmi", pmi, (flux_free_f) pmi_destroy) < 0) {
        pmi_destroy (pmi);
        return -1;
    }
    return 0;
}

/* Prepend 'path' to the environment variable 'name' which is assumed to
 * be a colon-separated list. If 'name' isn't already set, set it to 'path'.
 * Return 0 on success, -1 on failure.
 */
static int prepend_path_to_cmd_env (flux_cmd_t *cmd,
                                    const char *name,
                                    const char *path)
{
    const char *searchpath = flux_cmd_getenv (cmd, name);

    return flux_cmd_setenvf (cmd,
                             1,
                             name,
                             "%s%s%s",
                             path,
                             searchpath ? ":" : "",
                             searchpath ? searchpath : "");
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

    if (!(pmi = flux_plugin_aux_get (p, "pmi")))
        return 0; // plugin disabled
    if (!(shell = flux_plugin_get_shell (p))
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
    const char *pmipath;
    if (!(pmipath = flux_conf_builtin_get ("pmi_library_path", FLUX_CONF_AUTO)))
        return -1;
    /* Flux libpmi.so and libpmi2.so are installed to a directory outside
     * of the default ld.so search path.  Add this directory to LD_LIBRARY_PATH
     * so Flux jobs find Flux PMI libs before Slurm's PMI libs which are in
     * the system path (a tripping hazard).
     * N.B. The cray-pals plugin in flux-coral2 will need to undo this
     * so Cray MPICH finds the Cray libpmi2.so first which uses libpals.
     * See also flux-framework/flux-core#5714.
     */
    char *cpy;
    char *pmidir;
    if (!(cpy = strdup (pmipath))
        || !(pmidir = dirname (cpy))
        || prepend_path_to_cmd_env (cmd, "LD_LIBRARY_PATH", pmidir) < 0) {
        free (cpy);
        return -1;
    }
    free (cpy);
    /* N.B. The pre-v5 OpenMPI flux MCA plugin dlopens the library pointed to
     * by FLUX_PMI_LIBRARY_PATH.  Since the library only works when this shell
     * plugin is active, set it here.
     */
    if (flux_cmd_setenvf (cmd, 1, "FLUX_PMI_LIBRARY_PATH", "%s", pmipath) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_pmi = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = shell_pmi_init,
    .task_init = shell_pmi_task_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
