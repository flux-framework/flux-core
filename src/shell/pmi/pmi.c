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
 * pmi simple server, and creates a jobdict to serve as the PMI KVS.
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
 * These are kvs_get, kvs_put, and barrier, all delegated to the jobdict
 * plugin through the plugin stack.  Although the task
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
#include <flux/idset.h>
#include <jansson.h>

#include "src/common/libpmi/simple_server.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "builtins.h"
#include "internal.h"
#include "task.h"

struct shell_pmi {
    flux_shell_t *shell;
    struct pmi_simple_server *server;
    bool abort;     // an abort exception has been raised
    bool nomap;     // pmi-simple.nomap: omit task mapping keys
};

/* The PMI KVS is a jobdict named after the plugin.  Its puts, gets, and
 * barriers are delegated to the builtin jobdict plugin through the plugin
 * stack (a PMI barrier is a jobdict exchange), and exchange completion is
 * delivered to the topic below.  The "flux" jobdict is reachable through
 * it, so a get of flux.KEY is served from there.
 */
#define PMI_JOBDICT FLUX_SHELL_PLUGIN_NAME
#define PMI_EXCHANGE_EXIT_TOPIC "pmi-simple.exchange-exit"

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
    struct shell_pmi *pmi = arg;

    /*  Attempt to raise job exception and return to the shell's reactor.
     *   This allows the shell to continue to process events and stdio
     *   until the exec system terminates the job due to the exception.
     *
     *  Note that the job is now being torn down. Flag that an abort was
     *   raised so that PMI protocol errors from the other tasks as they are
     *   killed are not treated as a fatal shell error (see pmi_fd_cb()).
     */
    pmi->abort = true;
    flux_shell_raise ("exec",
                      0,
                      "PMI_Abort%s%s",
                      msg ? ": " : "",
                      msg ? msg : "");
}

/**
 ** ops for implementing the PMI KVS on a jobdict
 ** This is used if pmi.kvs=exchange option is provided.
 **/

/* Call a jobdict topic through the plugin stack with packed IN args.
 * On success, *argsp is set to the args (caller destroys) so OUT values
 * may be read.  On failure, log the jobdict errmsg and return -1.
 */
static int jobdict_call (struct shell_pmi *pmi,
                         const char *topic,
                         flux_plugin_arg_t **argsp,
                         const char *fmt,
                         ...)
{
    flux_plugin_arg_t *args;
    va_list ap;
    int rc;
    const char *errmsg = NULL;

    if (!(args = flux_plugin_arg_create ()))
        return -1;
    va_start (ap, fmt);
    rc = flux_plugin_arg_vpack (args, FLUX_PLUGIN_ARG_IN, fmt, ap);
    va_end (ap);
    if (rc < 0)
        goto error;
    if (flux_shell_plugstack_call (pmi->shell, topic, args) < 0) {
        (void)flux_plugin_arg_unpack (args,
                                      FLUX_PLUGIN_ARG_OUT,
                                      "{s:s}",
                                      "errmsg", &errmsg);
        shell_warn ("%s: %s", topic, errmsg ? errmsg : "failed");
        goto error;
    }
    if (argsp)
        *argsp = args;
    else
        flux_plugin_arg_destroy (args);
    return 0;
error:
    flux_plugin_arg_destroy (args);
    return -1;
}

/* Store a key in the PMI jobdict.  A 'local' key is visible only on this
 * shell; others are exchanged at PMI barriers.
 */
static int put_dict (struct shell_pmi *pmi,
                     const char *key,
                     const char *val,
                     bool local)
{
    return jobdict_call (pmi,
                         "jobdict.put",
                         NULL,
                         "{s:s s:s s:s s:b}",
                         "name", PMI_JOBDICT,
                         "key", key,
                         "value", val,
                         "local", local ? 1 : 0);
}

/* The jobdict exchange tag is the PMI barrier group string, or
 * PMI_GLOBAL_TAG for the legacy whole-job barrier (NULL group).
 */
#define PMI_GLOBAL_TAG "global"

static const char *tag_to_group (const char *tag)
{
    return streq (tag, PMI_GLOBAL_TAG) ? NULL : tag;
}

/* The jobdict exchange has completed on this shell (successfully or not).
 */
static int exchange_exit_cb (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    struct shell_pmi *pmi = data;
    const char *tag;
    const char *errmsg = NULL;
    int rc = 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s?s}",
                                "tag", &tag,
                                "errmsg", &errmsg) < 0) {
        shell_warn ("error unpacking exchange exit args");
        return -1;
    }
    if (errmsg) {
        shell_warn ("exchange %s: %s", tag, errmsg);
        rc = -1;
    }
    pmi_simple_server_barrier_complete (pmi->server, tag_to_group (tag), rc);
    return 0;
}

/* pmi_simple_ops->kvs_get() signature */
static int exchange_kvs_get (void *arg,
                             void *cli,
                             const char *kvsname,
                             const char *key)
{
    struct shell_pmi *pmi = arg;
    flux_plugin_arg_t *args;
    const char *val = NULL;

    /* pmi-simple.nomap omits the task mapping keys, which a Flux client
     * uses to set broker.mapping.  PMI_process_mapping is simply never
     * put; flux.taskmap is published by the fluxkeys plugin and must be
     * hidden here.
     */
    if (pmi->nomap && streq (key, "flux.taskmap"))
        return -1; // PMI_ERR_INVALID_KEY
    if (jobdict_call (pmi,
                      "jobdict.get",
                      &args,
                      "{s:s s:s}",
                      "name", PMI_JOBDICT,
                      "key", key) < 0)
        return -1; // PMI_ERR_INVALID_KEY
    /* A value that does not fit in a PMI-1 get_result response is treated
     * as not found rather than truncated.  The client handles a missing
     * flux.taskmap by falling back to PMI_process_mapping.
     */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "value", &val) < 0
        || strlen (val) > SIMPLE_KVS_VAL_MAX) {
        flux_plugin_arg_destroy (args);
        return -1; // PMI_ERR_INVALID_KEY (not found)
    }
    pmi_simple_server_kvs_get_complete (pmi->server, cli, val);
    flux_plugin_arg_destroy (args);
    return 0;
}

/* Map an explicit group's ranks (the part of 'group' before :tag) to
 * the shells that host them.  Returns a new idset, or NULL on failure.
 */
static struct idset *group_shells (struct shell_pmi *pmi, const char *group)
{
    struct taskmap *map = pmi->shell->info->taskmap;
    const char *colon;
    char *prefix = NULL;
    struct idset *members = NULL;
    struct idset *shells = NULL;
    unsigned int rank;

    if (!(colon = strrchr (group, ':'))
        || !(prefix = strndup (group, colon - group))
        || !(members = idset_decode (prefix))
        || !(shells = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto error;
    rank = idset_first (members);
    while (rank != IDSET_INVALID_ID) {
        int shell_rank = taskmap_nodeid (map, rank);
        if (shell_rank < 0 || idset_set (shells, shell_rank) < 0)
            goto error;
        rank = idset_next (members, rank);
    }
    free (prefix);
    idset_destroy (members);
    return shells;
error:
    ERRNO_SAFE_WRAP (free, prefix);
    idset_destroy (members);
    idset_destroy (shells);
    return NULL;
}

/* pmi_simple_ops->barrier_enter() signature.
 *
 * Run a jobdict exchange over the shells in the barrier group: all shells
 * for the legacy barrier and WORLD, this shell for NODE, and the shells
 * hosting the members for an explicit group.  The exchange is keyed by
 * the group string and completes via exchange_exit_cb().
 */
static int exchange_barrier_enter (void *arg, const char *group)
{
    struct shell_pmi *pmi = arg;
    const char *tag = group ? group : PMI_GLOBAL_TAG;
    struct idset *participants = NULL;
    char *participants_str = NULL;
    int rc = -1;

    if (group && strncmp (group, "WORLD", 5) != 0) {
        if (strncmp (group, "NODE", 4) == 0) {
            if (!(participants = idset_create (0, IDSET_FLAG_AUTOGROW))
                || idset_set (participants,
                              pmi->shell->info->shell_rank) < 0) {
                shell_warn ("NODE barrier: %s", flux_strerror (errno));
                goto out;
            }
        }
        else if (!(participants = group_shells (pmi, group))) {
            shell_warn ("group barrier: cannot map members to shells: %s",
                        flux_strerror (errno));
            goto out;
        }
        if (!(participants_str = idset_encode (participants,
                                               IDSET_FLAG_RANGE))) {
            shell_warn ("group barrier: %s", flux_strerror (errno));
            goto out;
        }
    }
    if (jobdict_call (pmi,
                      "jobdict.exchange",
                      NULL,
                      "{s:s s:s s:s? s:s}",
                      "name", PMI_JOBDICT,
                      "tag", tag,
                      "participants", participants_str,
                      "exit_topic", PMI_EXCHANGE_EXIT_TOPIC) < 0)
        goto out;
    rc = 0;
out:
    free (participants_str);
    idset_destroy (participants);
    return rc; // 0 on success, -1 (PMI_FAIL) on error
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
    return put_dict (pmi, key, val, strstarts (key, "local::"));
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
        /*  If an abort exception was already raised, the job is being torn
         *   down and protocol errors from the tasks being killed are
         *   expected. Don't let them override the abort exit status with a
         *   fatal shell error (flux-framework/flux-core#7406).
         */
        if (!pmi->abort)
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
    put_dict (pmi, "PMI_process_mapping", s, true);
out:
    free (s);
    return 0;
}

static void pmi_destroy (struct shell_pmi *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        pmi_simple_server_destroy (pmi->server);
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

static struct shell_pmi *pmi_create (flux_plugin_t *p, json_t *config)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_pmi *pmi;
    struct shell_info *info = shell->info;
    const struct idset *local_ranks;
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
    if (streq (kvs, "exchange")) {
        shell_pmi_ops.kvs_put = exchange_kvs_put;
        shell_pmi_ops.kvs_get = exchange_kvs_get;
        shell_pmi_ops.barrier_enter = exchange_barrier_enter;
        if (flux_plugin_add_handler (p,
                                     PMI_EXCHANGE_EXIT_TOPIC,
                                     exchange_exit_cb,
                                     pmi) < 0)
            goto error;
        if (jobdict_call (pmi,
                          "jobdict.create",
                          NULL,
                          "{s:s s:i s:[s]}",
                          "name", PMI_JOBDICT,
                          "k", exchange_k,
                          "reach", "flux") < 0)
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
    local_ranks = taskmap_taskids (info->taskmap, info->shell_rank);
    if (!(pmi->server = pmi_simple_server_create (shell_pmi_ops,
                                                  0, // appnum
                                                  info->total_ntasks,
                                                  info->rankinfo.ntasks,
                                                  local_ranks,
                                                  kvsname,
                                                  flags,
                                                  pmi)))
        goto error;
    pmi->nomap = nomap;
    if (!nomap && init_clique (pmi) < 0)
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

    if (!(pmi = pmi_create (p, config)))
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
