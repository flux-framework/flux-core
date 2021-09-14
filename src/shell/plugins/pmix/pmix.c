/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pmix.c - main pmix shell plugin
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <pthread.h>
#include <flux/core.h>
#include <pmix_server.h>
#include <pmix.h>

#include "internal.h"
#include "task.h"

#include "src/shell/pmi/pmi_exchange.h"

#include "server.h"
#include "synchro.h"
#include "infovec.h"
#include "map.h"
#include "dmodex.h"
#include "codec.h"

#define PP_NSPACE_NAME  "flux"

struct client {
    pmix_proc_t proc;
};

struct pp {
    flux_shell_t *shell;
    struct psrv *psrv;
    zlist_t *clients;
    struct pmi_exchange *pmi_exchange;
};

/* This has to be global, since pmix_server_module_t callbacks don't have a way
 * to pass in a user-supplied pointer.
 */

static struct pp *global_plugin_ctx;

static int client_connected_cb (const pmix_proc_t *proc,
                                void *server_object,
                                pmix_op_cbfunc_t cbfunc,
                                void *cbdata)
{
    //struct client *cli = server_object;

    if (cbfunc)
        cbfunc (PMIX_SUCCESS, cbdata);
    return 0;
}

static int client_finalized_cb (const pmix_proc_t *proc,
                                void *server_object,
                                pmix_op_cbfunc_t cbfunc,
                                void *cbdata)
{
    //struct client *cli = server_object;

    if (cbfunc)
        cbfunc (PMIX_SUCCESS, cbdata);
    return 0;
}

static int abort_cb (const pmix_proc_t *proc,
                     void *server_object,
                     int status,
                     const char msg[],
                     pmix_proc_t procs[],
                     size_t nprocs,
                     pmix_op_cbfunc_t cbfunc,
                     void *cbdata)
{
    struct client *cli = server_object;

    shell_die (1, "pmix: rank %d called abort: %s", cli->proc.rank, msg);

    if (cbfunc)
        cbfunc (PMIX_SUCCESS, cbdata);
    return 0;
}

/* Context used to pass upcall's callback pointers to rpc continuation,
 * so it can make the upcall's callback after receiving a response.
 * This is used by both fence_nb and direct_modex upcalls, since they have
 * the same callback signature.
 */
struct modex_ctx {
    pmix_modex_cbfunc_t cbfunc;
    void *cbdata;
};

void modex_ctx_destroy (struct modex_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx);
        errno = saved_errno;
    }
}

static struct modex_ctx *modex_ctx_create (pmix_modex_cbfunc_t cbfunc,
                                           void *cbdata)
{
    struct modex_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->cbfunc = cbfunc;
    ctx->cbdata = cbdata;
    return ctx;
}

static json_t *dict_create (int shell_rank, void *data, size_t size)
{
    char key[16];
    json_t *xdata;
    json_t *dict = NULL;

    snprintf (key, sizeof (key), "%d", shell_rank);
    if ((xdata = pp_data_encode (data, size))) {
        dict = json_pack ("{s:O}", key, xdata);
        json_decref (xdata);
    }
    return dict;
}

static int dict_concat (json_t *dict, void **datap, size_t *sizep)
{
    const char *key;
    json_t *value;
    uint8_t *data;
    ssize_t bufsize = 0;
    ssize_t size = 0;

    json_object_foreach (dict, key, value) {
        ssize_t chunk;
        if ((chunk = pp_data_decode_bufsize (value)) < 0)
            return -1;
        bufsize += chunk;
    }
    if (!(data = malloc (bufsize)))
        return -1;

    json_object_foreach (dict, key, value) {
        ssize_t chunk;
        chunk = pp_data_decode_tobuf (value, data + size, bufsize - size);
        if (chunk < 0)
            goto error;
        size += chunk;
    }
    *datap = data;
    *sizep = size;
    return 0;
error:
    free (data);
    return -1;
}

static void exchange_cb (struct pmi_exchange *pex, void *arg)
{
    struct modex_ctx *ctx = arg;

    if (pmi_exchange_has_error (pex)) {
        shell_warn ("pmix: exchange failed");
        goto error;
    }
    if (ctx->cbfunc) {
        void *data = NULL;
        size_t ndata;
        if (dict_concat (pmi_exchange_get_dict (pex), &data, &ndata) < 0) {
            shell_warn ("pmix: error processing exchanged dict");
            goto error;
        }
        ctx->cbfunc (PMIX_SUCCESS, data, ndata, ctx->cbdata, free, data);
    }
    return;
error:
    if (ctx->cbfunc)
        ctx->cbfunc (PMIX_ERROR, NULL, 0, ctx->cbdata, NULL, NULL);
}

static int fence_nb_cb (const pmix_proc_t procs[],
                        size_t nprocs,
                        const pmix_info_t info[],
                        size_t ninfo,
                        char *data,
                        size_t ndata,
                        pmix_modex_cbfunc_t cbfunc,
                        void *cbdata)
{
    struct pp *pp = global_plugin_ctx;
    struct modex_ctx *ctx = NULL;
    json_t *dict = NULL;
    int rc = PMIX_ERROR;
    int i;

    /* Internal exchange implementation participation of all shells,
     * therefore all procs must participate since o/w a shell with no
     * participation from local procs would not get the upcall.
     * N.B. A user call to PMIx_Fence (NULL, ...) is converted to wildcard
     * before we see it.
     */
    if (nprocs > 1 || procs[0].rank != PMIX_RANK_WILDCARD) {
        shell_warn ("pmix: fence over proc subset is not supported by flux");
        rc = PMIX_ERR_NOT_SUPPORTED;
        goto error;
    }

    /* Process any info options from the server upcall.
     * Ensure that all required attributes are accepted,
     * even if we do nothing about them at this point.
     */
    for (i = 0; i < ninfo; i++) {
        if (!strcmp (info[i].key, PMIX_COLLECT_DATA)) {
            if (info[i].value.type != PMIX_BOOL)
                goto error;
            shell_debug ("pmix: ignoring fence_nb %s", info[i].key);
        }
        else if (!strcmp (info[i].key, PMIX_COLLECT_GENERATED_JOB_INFO)) {
            if (info[i].value.type != PMIX_BOOL)
                goto error;
            shell_debug ("pmix: ignoring fence_nb %s", info[i].key);
        }
        else {
            int required = (info[i].flags & PMIX_INFO_REQD);
            shell_warn ("pmix: unknown %s fence_nb info key: %s",
                        required ? "required" : "optional",
                        info[i].key);
            if (required)
                goto error;
        }
    }

    if (!(dict = dict_create (pp->shell->info->shell_rank, data, ndata))
        || !(ctx = modex_ctx_create (cbfunc, cbdata))
        || pmi_exchange (pp->pmi_exchange, dict, exchange_cb, ctx) < 0) {
        modex_ctx_destroy (ctx);
        shell_warn ("pmix: error initiating exchange");
        goto error;
    }
    json_decref (dict);
    return 0;
error:
    if (cbfunc)
        cbfunc (rc, NULL, 0, cbdata, NULL, NULL);
    json_decref (dict);
    return 0; // N.B. func return value is ignored - use cbfunc only
}

/* Finalize a direct modex upcall using info obtained from
 * pmix-dmodex RPC to another shell.
 */
static void direct_modex_continuation (flux_future_t *f, void *arg)
{
    struct modex_ctx *ctx = flux_future_aux_get (f, "modex_ctx");
    void *data;
    int ndata;
    int status;

    if (pp_dmodex_get_status (f, &status) < 0) {
        if (errno == ETIMEDOUT)
            status = PMIX_ERR_TIMEOUT;
        else
            status = PMIX_ERROR;
        goto error;
    }
    if (status != PMIX_SUCCESS)
        goto error;
    if (ctx->cbfunc) {
        /* pp_dmodex_get_data() allocates data, then server releases it with
         * free() b/c we tell it to with the last two cbfunc args
         */
        if (pp_dmodex_get_data (f, &data, &ndata) < 0) {
            status = PMIX_ERROR;
            goto error;
        }
        ctx->cbfunc (PMIX_SUCCESS, data, ndata, ctx->cbdata, free, data);
    }
    flux_future_destroy (f);
    return;
error:
    if (ctx->cbfunc)
        ctx->cbfunc (status, NULL, 0, ctx->cbdata, NULL, NULL);
    flux_future_destroy (f);
}

/* This is the direct_modex upcall from the server.
 * It makes the pmix-dmodex rpc, then continues executing
 * in direct_modex_continuation() when response is received.
 */
static int direct_modex_cb (const pmix_proc_t *proc,
                            const pmix_info_t info[],
                            size_t ninfo,
                            pmix_modex_cbfunc_t cbfunc,
                            void *cbdata)
{
    struct pp *pp = global_plugin_ctx;
    struct modex_ctx *ctx = NULL;
    flux_future_t *f = NULL;
    double timeout = -1; // seconds, -1 to disable
    int rc = PMIX_ERROR;
    int i;

    /* Process any info options from the server upcall.
     */
    for (i = 0; i < ninfo; i++) {
        /* Optional timeout (seconds, 0=infinite)
         * The future will be fulfilled with ETIMEDOUT if the timer
         * expires before a response is received.
         */
        if (!strcmp (info[i].key, "pmix.timeout")) {
            if (info[i].value.type != PMIX_INT)
                goto error;
            if (timeout > 0)
                timeout = info[i].value.data.integer;
        }
        /* FIXME: This opt is flagged as required, and advises us to wait for
         * a particular key to be posted by a client before responding to the
         * direct modex request upcall, but there doesn't seem to be an
         * interface to ask the pmix server to wait for a particular key,
         * for example, by passing this info into PMIx_server_dmodex_request(),
         * and the modex data is opaque from our perspective, so how do we know?
         * Probably missing something.  N.B. added in pmix spec v4.0 (dec 2020)
         */
        else if (!strcmp (info[i].key, "pmix.req.key")) {
            if (info[i].value.type != PMIX_STRING)
                goto error;
            int required = (info[i].flags & PMIX_INFO_REQD);
            shell_debug ("pmix: ignoring %s dmodex %s=%s argument",
                        required ? "required" : "optional",
                        info[i].key,
                        info[i].value.data.string);
        }
        else {
            int required = (info[i].flags & PMIX_INFO_REQD);
            shell_warn ("pmix: unknown %s dmodex %s argument",
                        required ? "required" : "optional",
                        info[i].key);
            if (required)
                goto error;
        }
    }

    if (!(f = pp_dmodex (pp->shell, proc))
        || !(ctx = modex_ctx_create (cbfunc, cbdata))
        || flux_future_then (f, timeout, direct_modex_continuation, pp) < 0
        || flux_future_aux_set (f,
                                "modex_ctx",
                                ctx,
                                (flux_free_f)modex_ctx_destroy) < 0) {
        shell_warn ("pmix: error initiating pmix-modex RPC");
        modex_ctx_destroy (ctx);
        flux_future_destroy (f);
        goto error;
    }
    return 0;
error:
    if (cbfunc)
        cbfunc (rc, NULL, 0, cbdata, NULL, NULL);
    return 0; // N.B. func return value is ignored - use cbfunc only
}

static void error_cb (size_t evhdlr_registration_id,
                      pmix_status_t status,
                      const pmix_proc_t *source,
                      pmix_info_t info[],
                      size_t ninfo,
                      pmix_info_t results[],
                      size_t nresults,
                      pmix_event_notification_cbfunc_fn_t cbfunc,
                      void *cbdata)
{
    shell_warn ("pmix: rank %d error: %s",
                source->rank, PMIx_Error_string (status));
}

static pmix_server_module_t callbacks = {
    .client_connected = client_connected_cb,
    .client_finalized = client_finalized_cb,
    .abort = abort_cb,
    .fence_nb = fence_nb_cb,
    .direct_modex = direct_modex_cb,
};

static void client_destroy (struct client *cli)
{
    if (cli) {
        int saved_errno = errno;
        free (cli);
        errno = saved_errno;
    }
}

static struct client *client_create (int rank)
{
    struct client *cli;

    if (!(cli = calloc (1, sizeof (*cli))))
        return NULL;

    cli->proc.rank = rank;
    strncpy (cli->proc.nspace, PP_NSPACE_NAME, PMIX_MAX_NSLEN);

    return cli;
}

static void pp_destroy (struct pp *pp)
{
    if (pp) {
        int saved_errno = errno;
        if (pp->clients) {
            struct client *cli;
            while ((cli = zlist_pop (pp->clients)))
                client_destroy (cli);
            zlist_destroy (&pp->clients);
        }
        pp_server_destroy (pp->psrv);
        pmi_exchange_destroy (pp->pmi_exchange);
        global_plugin_ctx = NULL;
        free (pp);
        errno = saved_errno;
    }
}

/* Register nspace.
 * N.B. this must complete synchronously, before tasks are started.
 */
static int register_nspace (flux_shell_t *shell)
{
    struct infovec *iv = NULL;
    int job_size = shell->info->total_ntasks;
    int local_size = shell->info->rankinfo.ntasks;
    char *local_peers = NULL;
    char *proc_map = NULL;
    char *node_map = NULL;
    char jobid[32];
    struct synchro sync;
    int rc;
    int ret = -1;

    if (!(local_peers = pp_map_local_peers (shell->info->shell_rank,
                                            shell->info->rcalc))
        || !(proc_map = pp_map_proc_create (shell->info->shell_size,
                                            shell->info->rcalc))
        || !(node_map = pp_map_node_create (shell->info->R))
        || flux_job_id_encode (shell->jobid, "f58", jobid, sizeof (jobid)) < 0) {
        shell_warn ("pmix: error preparing nspace maps");
        goto done;
    }

    if (shell->info->shell_rank == 0) {
        shell_debug ("job_size %d", job_size);
        shell_debug ("proc_map %s", proc_map);
        shell_debug ("node_map %s", node_map);
    }
    shell_debug ("local_size %d", local_size);
    shell_debug ("local_peers %s", local_peers);

    if (!(iv = pp_infovec_create ())
        || pp_infovec_set_u32 (iv, PMIX_UNIV_SIZE, job_size) < 0
        || pp_infovec_set_str (iv, PMIX_JOBID, jobid) < 0
        || pp_infovec_set_u32 (iv, PMIX_JOB_SIZE, job_size) < 0
        || pp_infovec_set_u32 (iv, PMIX_MAX_PROCS, job_size) < 0
        || pp_infovec_set_str (iv, PMIX_PROC_MAP, proc_map) < 0
        || pp_infovec_set_str (iv, PMIX_NODE_MAP, node_map) < 0
        || pp_infovec_set_u32 (iv, PMIX_LOCAL_SIZE, local_size) < 0
        || pp_infovec_set_str (iv, PMIX_LOCAL_PEERS, local_peers) < 0) {
        shell_warn ("pmix: error setting info vector");
        goto done;
    }

    /* If rc is PMIX_SUCCESS, wait for callback to get final rc value.
     * If rc is anything else, function is done and callback won't be called.
     * There are two success values.
     */
    pp_synchro_init (&sync);
    rc = PMIx_server_register_nspace (PP_NSPACE_NAME,
                                      local_size,
                                      pp_infovec_info (iv),
                                      pp_infovec_count (iv),
                                      pp_synchro_signal,
                                      &sync);
    if (rc == PMIX_SUCCESS)
        rc = pp_synchro_wait (&sync);
    if (rc != PMIX_SUCCESS && rc != PMIX_OPERATION_SUCCEEDED) {
        shell_warn ("pmix: PMIx_server_register_nspace: %s",
                    PMIx_Error_string (rc));
        goto done;
    }
    ret = 0;
done:
    free (node_map);
    free (proc_map);
    free (local_peers);
    pp_infovec_destroy (iv);
    return ret;
}

static struct psrv *initialize_pmix_server (struct pp *pp)
{
    const char *tmpdir;
    struct infovec *iv = NULL;
    struct psrv *srv = NULL;

    if (!(tmpdir = flux_shell_getenv (pp->shell, "FLUX_JOB_TMPDIR"))) {
        shell_warn ("pmix: FLUX_JOB_TMPDIR is not set");
        return NULL;
    }
    if (!(iv = pp_infovec_create ())
        || pp_infovec_set_str (iv, PMIX_SERVER_NSPACE, PP_NSPACE_NAME) < 0
        || pp_infovec_set_rank (iv, PMIX_SERVER_RANK,
                                pp->shell->info->shell_rank) < 0
        || pp_infovec_set_str (iv, PMIX_SYSTEM_TMPDIR, tmpdir) < 0
        || pp_infovec_set_bool (iv, PMIX_SERVER_TOOL_SUPPORT, false) < 0
        || pp_infovec_set_bool (iv, PMIX_SERVER_SYSTEM_SUPPORT, false) < 0
        || pp_infovec_set_bool (iv, PMIX_SERVER_SESSION_SUPPORT, false) < 0
        || pp_infovec_set_bool (iv, PMIX_SERVER_GATEWAY, false) < 0
        || pp_infovec_set_bool (iv, PMIX_SERVER_SCHEDULER, false) < 0
        || pp_infovec_set_str (iv, PMIX_SERVER_TMPDIR, tmpdir) < 0) {
        shell_warn ("pmix: error setting up info array");
        goto done;
    }
    srv = pp_server_create (flux_get_reactor (pp->shell->h),
                            iv,
                            &callbacks,
                            error_cb,
                            pp);
done:
    pp_infovec_destroy (iv);
    return srv;
}

static struct pp *pp_create (flux_shell_t *shell)
{
    struct pp *pp;

    if (!(pp = calloc (1, sizeof (*pp))))
        return NULL;
    pp->shell = shell;
    if (!(pp->clients = zlist_new ()))
        goto error;
    if (!(pp->psrv = initialize_pmix_server (pp))) {
        shell_warn ("pmix: could not initialize pmix server");
        goto error;
    }
    if (pp_dmodex_service_register (shell, pp->psrv) < 0) {
        shell_warn ("pmix: failed to register dmodex service");
        goto error;
    }
    if (register_nspace (shell) < 0) {
        shell_warn ("pmix: failed to register nspace");
        goto error;
    }
    if (!(pp->pmi_exchange = pmi_exchange_create (shell, 0))) {
        shell_warn ("pmix: failed to create exchange context");
        goto error;
    }
    global_plugin_ctx = pp;
    return pp;
error:
    pp_destroy (pp);
    return NULL;
}

static int pp_init (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *arg,
                    void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct pp *pp;
    if (!shell || !(pp = pp_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p, "pp", pp, (flux_free_f) pp_destroy) < 0) {
        pp_destroy (pp);
        return -1;
    }
    return 0;
}

static int pp_task_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *arg)
{
    flux_shell_t *shell;
    struct pp *pp;
    flux_shell_task_t *task;
    flux_cmd_t *cmd;
    char **env = NULL;
    int rank;
    struct client *cli;
    int rc;

    if (!(shell = flux_plugin_get_shell (p))
        || !(pp = flux_plugin_aux_get (p, "pp"))
        || !(task = flux_shell_current_task (shell))
        || !(cmd = flux_shell_task_cmd (task))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return -1;

    if (!(cli = client_create (rank))
        || zlist_append (pp->clients, cli) < 0) {
        client_destroy (cli);
        return -1;
    }

    /* Set pmix related environment variables.
     */
    if ((rc = PMIx_server_setup_fork (&cli->proc, &env)) != PMIX_SUCCESS) {
        shell_warn ("pmix: PMIx_server_setup_fork: %s",
                    PMIx_Error_string (rc));
        return -1;
    }
    if (env) {
        int i;
        for (i = 0; env[i] != NULL; i++) {
            char *name = env[i];
            char *value = strchr (name, '=');
            if (value)
                *value++ = '\0';
            if (flux_cmd_setenvf (cmd, 1, name, "%s", value) < 0) {
                shell_warn ("pmix: setenv %s failed", name);
                return -1;
            }
        }
        free (env);
    }

    /* Register the client with the server.
     * If rc is PMIX_SUCCESS, wait for callback to get final rc value.
     * If rc is anything else, function is done and callback won't be called.
     * There are two success values.
     */
    struct synchro sync;
    pp_synchro_init (&sync);
    rc = PMIx_server_register_client (&cli->proc,
                                      getuid (),
                                      getgid (),
                                      cli, // handle passed back to us
                                      pp_synchro_signal,
                                      &sync);
    if (rc == PMIX_SUCCESS)
        rc = pp_synchro_wait (&sync);
    if (rc != PMIX_SUCCESS && rc != PMIX_OPERATION_SUCCEEDED) {
        shell_warn ("pmix: PMIx_server_register_client: %s",
                    PMIx_Error_string (rc));
        return -1;
    }
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_set_name (p, "pmix") < 0
        || flux_plugin_add_handler (p, "shell.init", pp_init, NULL) < 0
        || flux_plugin_add_handler (p, "task.init", pp_task_init, NULL) < 0) {
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
