/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* server.c - pmix server thread ops
 *
 * pmix server callbacks are invoked in server thread context.  Use a 0MQ
 * inproc (shared memory) socket to send callback parameters to the shell
 * thread where an identical callback is invoked.  The shell end plays
 * nicely with the flux reactor and can be oblivious to MT-safety issues
 * contained in this source module.
 *
 * Each callback has a matched send and receive function.  Support for
 * individual callbacks are being added as needed, as they are a little bit
 * laborious to write.
 *
 * To add another callback:
 * - implement send function and add entry to send_callbacks[] for
 *   pmix_server_module_t callbacks, or wrapper function for other functions
 * - implement recv function and add entry to recv_callbacks[]
 *
 * Other notes:
 * - it is safe to call pmix completion callbacks and API functions
 *   from the shell thread.  In fact, completion function pointers are
 *   tranferred as json integers over the socket and invoked from the shell.
 *
 * - it is *not* safe to call shell functions from the server thread,
 *   e.g. don't call shell_warn() from send functions below!
 *
 * - the 0MQ socket pair is created and destroyed in the shell thread,
 *   but while the server thread is running, the server socket end may *only*
 *   be used from the server thread.  0MQ sockets are generally not MT-safe.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <stdarg.h>
#include <jansson.h>
#include <flux/core.h>
#include <pmix.h>
#include <pmix_server.h>
#include <czmq.h>
#include <zmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libzmqutil/reactor.h"
#include "src/common/libzmqutil/msg_zsock.h"

#include "internal.h"
#include "task.h"

#include "server.h"
#include "infovec.h"
#include "codec.h"
#include "socketpair.h"

typedef void (*recv_cb_f)(struct psrv *psrv, const flux_msg_t *msg);
// an entry in the recv_callbacks[] table
struct recv_cb {
    const char *name;
    recv_cb_f fun;
};

struct psrv {
    struct socketpair *sp;
    flux_watcher_t *w;
    pmix_server_module_t *callbacks;
    pmix_notification_fn_t error_cb;
    void *callback_arg;
};

/* This has to be global, since pmix_server_module_t callbacks don't have a way
 * to pass in a user-supplied pointer.
 */
static struct psrv *global_server_ctx;

/**
 ** client_connected callback
 **/

static void recv_client_connected (struct psrv *psrv, const flux_msg_t *msg)
{
    json_t *xproc;
    json_t *xserver_object;
    json_t *xcbfunc;
    json_t *xcbdata;
    pmix_proc_t proc;
    void *server_object;
    pmix_op_cbfunc_t cbfunc;
    void *cbdata;
    int rc;

    if (flux_msg_unpack (msg,
                         "{s:o s:o s:o s:o}",
                         "proc", &xproc,
                         "server_object", &xserver_object,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking client_connected message");
        return;
    }
    if (pp_proc_decode (xproc, &proc) < 0
        || pp_pointer_decode (xserver_object, &server_object) < 0
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding client_connected message");
        rc = PMIX_ERROR;
    }
    else if (!psrv->callbacks->client_connected)
        rc = PMIX_ERR_NOT_IMPLEMENTED;
    else {
        rc = psrv->callbacks->client_connected (&proc,
                                                server_object,
                                                cbfunc,
                                                cbdata);
    }
    if (rc != PMIX_SUCCESS) {
        if (cbfunc)
            cbfunc (rc, cbdata);
    }
}

static int send_client_connected (const pmix_proc_t *proc,
                                  void *server_object,
                                  pmix_op_cbfunc_t cbfunc,
                                  void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xproc;
    json_t *xserver_object = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;
    int rc = PMIX_SUCCESS;

    if (!(xproc = pp_proc_encode (proc))
        || !(xserver_object = pp_pointer_encode (server_object))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(xcbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "client_connected",
                                    "{s:O s:O s:O s:O}",
                                    "proc", xproc,
                                    "server_object", xserver_object,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata))
        rc = PMIX_ERROR;
    json_decref (xproc);
    json_decref (xserver_object);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    return rc;
}

/**
 ** client_finalized callback
 **/

static void recv_client_finalized (struct psrv *psrv, const flux_msg_t *msg)
{
    json_t *xproc;
    json_t *xserver_object;
    json_t *xcbfunc;
    json_t *xcbdata;
    void *server_object;
    pmix_op_cbfunc_t cbfunc;
    void *cbdata;
    pmix_proc_t proc;
    int rc;

    if (flux_msg_unpack (msg,
                         "{s:o s:o s:o s:o}",
                         "proc", &xproc,
                         "server_object", &xserver_object,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking client_finalized notification");
        return;
    }
    if (pp_proc_decode (xproc, &proc) < 0
        || pp_pointer_decode (xserver_object, &server_object) < 0
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding client_finalized notification");
        rc = PMIX_ERROR;
    }
    else if (!psrv->callbacks->client_finalized)
        rc = PMIX_ERR_NOT_IMPLEMENTED;
    else {
        rc = psrv->callbacks->client_finalized (&proc,
                                                server_object,
                                                cbfunc,
                                                cbdata);
    }
    if (rc != PMIX_SUCCESS) {
        if (cbfunc)
            cbfunc (rc, cbdata);
    }
}

static int send_client_finalized (const pmix_proc_t *proc,
                                  void *server_object,
                                  pmix_op_cbfunc_t cbfunc,
                                  void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xproc = NULL;
    json_t *xserver_object = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;
    int rc = PMIX_SUCCESS;

    if (!(xproc = pp_proc_encode (proc))
        || !(xserver_object = pp_pointer_encode (server_object))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(xcbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "client_finalized",
                                    "{s:O s:O s:O s:O}",
                                    "proc", xproc,
                                    "server_object", xserver_object,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata))
        rc = PMIX_ERROR;
    json_decref (xproc);
    json_decref (xserver_object);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    return rc;
}

/**
 ** abort callback
 **/

static void recv_abort (struct psrv *psrv, const flux_msg_t *msg)
{
    json_t *xproc;
    json_t *xserver_object;
    json_t *xprocs;
    json_t *xcbfunc;
    json_t *xcbdata;
    pmix_proc_t proc;
    void *server_object;
    pmix_proc_t *procs = NULL;
    size_t nprocs;
    int status;
    const char *message;
    pmix_op_cbfunc_t cbfunc;
    void *cbdata;
    int rc;

    if (flux_msg_unpack (msg,
                         "{s:o s:o s:i s:s s:o s:o s:o}",
                         "proc", &xproc,
                         "server_object", &xserver_object,
                         "status", &status,
                         "msg", &message,
                         "procs", &xprocs,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking abort notification");
        return;
    }
    if (pp_proc_decode (xproc, &proc) < 0
        || pp_pointer_decode (xserver_object, &server_object) < 0
        || pp_proc_array_decode (xprocs, &procs, &nprocs) < 0
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding abort notification");
        rc = PMIX_ERROR;
    }
    else if (!psrv->callbacks->abort)
        rc = PMIX_ERR_NOT_IMPLEMENTED;
    else {
        rc = psrv->callbacks->abort (&proc,
                                     server_object,
                                     status,
                                     message,
                                     procs,
                                     nprocs,
                                     cbfunc,
                                     cbdata);
    }
    if (rc != PMIX_SUCCESS) {
        if (cbfunc)
            cbfunc (rc, cbdata);
    }
    free (procs);
}

static int send_abort (const pmix_proc_t *proc,
                       void *server_object,
                       int status,
                       const char msg[],
                       pmix_proc_t procs[],
                       size_t nprocs,
                       pmix_op_cbfunc_t cbfunc,
                       void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xproc = NULL;
    json_t *xserver_object = NULL;
    json_t *xprocs = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;
    int rc = PMIX_SUCCESS;

    if (!(xproc = pp_proc_encode (proc))
        || !(xserver_object = pp_pointer_encode (server_object))
        || !(xprocs = pp_proc_array_encode (procs, nprocs))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(cbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "abort",
                                    "{s:O s:O s:i s:s s:O s:O s:O}",
                                    "proc", xproc,
                                    "server_object", xserver_object,
                                    "status", status,
                                    "msg", msg,
                                    "procs", xprocs,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata) < 0)
        rc = PMIX_ERROR;
    json_decref (xproc);
    json_decref (xserver_object);
    json_decref (xprocs);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    return rc;
}

/**
 ** fence_nb callback
 **/

static void recv_fence_nb (struct psrv *psrv, const flux_msg_t *msg)
{
    json_t *xprocs;
    json_t *xinfo;
    json_t *xdata;
    json_t *xcbfunc;
    json_t *xcbdata;
    pmix_proc_t *procs = NULL;
    size_t nprocs;
    struct infovec *iv = NULL;;
    void *data = NULL;
    size_t ndata;
    pmix_modex_cbfunc_t cbfunc;
    void *cbdata;
    int rc;

    if (flux_msg_unpack (msg,
                         "{s:o s:o s:o s:o s:o}",
                         "procs", &xprocs,
                         "info", &xinfo,
                         "data", &xdata,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking fence_nb notification");
        return;
    }
    if (pp_proc_array_decode (xprocs, &procs, &nprocs) < 0
        || !(iv = pp_infovec_create_from_json (xinfo))
        || pp_data_decode (xdata, &data, &ndata) < 0
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding fence_nb notification");
        rc = PMIX_ERROR;
    }
    else if (!psrv->callbacks->fence_nb) {
        rc = PMIX_ERR_NOT_IMPLEMENTED;
    }
    else {
        rc = psrv->callbacks->fence_nb (procs,
                                        nprocs,
                                        pp_infovec_info (iv),
                                        pp_infovec_count (iv),
                                        data,
                                        ndata,
                                        cbfunc,
                                        cbdata);
    }
    if (rc != PMIX_SUCCESS) {
        if (cbfunc)
            cbfunc (rc, NULL, 0, cbdata, NULL, NULL);
    }
    pp_infovec_destroy (iv);
    free (procs);
    free (data);
}

static int send_fence_nb (const pmix_proc_t procs[],
                          size_t nprocs,
                          const pmix_info_t info[],
                          size_t ninfo,
                          char *data,
                          size_t ndata,
                          pmix_modex_cbfunc_t cbfunc,
                          void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xprocs = NULL;
    json_t *xinfo = NULL;
    json_t *xdata = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;
    int rc = PMIX_SUCCESS;

    if (!(xprocs = pp_proc_array_encode (procs, nprocs))
        || !(xinfo = pp_info_array_encode (info, ninfo))
        || !(xdata = pp_data_encode (data, ndata))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(xcbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "fence_nb",
                                    "{s:O s:O s:O s:O s:O}",
                                    "procs", xprocs,
                                    "info", xinfo,
                                    "data", xdata,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata) < 0) {
        rc = PMIX_ERROR;
    }
    json_decref (xprocs);
    json_decref (xinfo);
    json_decref (xdata);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    return rc;
}

/**
 ** direct_modex callback
 **/

static void recv_direct_modex (struct psrv *psrv, const flux_msg_t *msg)
{
    json_t *xproc;
    json_t *xinfo;
    json_t *xcbfunc;
    json_t *xcbdata;
    pmix_proc_t proc;
    pmix_modex_cbfunc_t cbfunc;
    void *cbdata;
    struct infovec *iv = NULL;
    int rc;

    if (flux_msg_unpack (msg,
                         "{s:o s:o s:o s:o}",
                         "proc", &xproc,
                         "info", &xinfo,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking direct_modex notification");
        return;
    }
    else if (pp_proc_decode (xproc, &proc) < 0
        || !(iv = pp_infovec_create_from_json (xinfo))
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding direct_modex notification");
        rc = PMIX_ERROR;
    }
    else if (!psrv->callbacks->direct_modex)
        rc = PMIX_ERR_NOT_IMPLEMENTED;
    else {
        rc = psrv->callbacks->direct_modex (&proc,
                                            pp_infovec_info (iv),
                                            pp_infovec_count (iv),
                                            cbfunc,
                                            cbdata);
    }
    if (rc != PMIX_SUCCESS) {
        if (cbfunc)
            cbfunc (rc, NULL, 0, cbdata, NULL, NULL);
    }
    pp_infovec_destroy (iv);
}

static int send_direct_modex (const pmix_proc_t *proc,
                              const pmix_info_t info[],
                              size_t ninfo,
                              pmix_modex_cbfunc_t cbfunc,
                              void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xproc  = NULL;
    json_t *xinfo = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;
    int rc = PMIX_SUCCESS;

    if (!(xproc = pp_proc_encode (proc))
        || !(xinfo = pp_info_array_encode (info, ninfo))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(xcbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "direct_modex",
                                    "{s:O s:O s:O s:O}",
                                    "proc", xproc,
                                    "info", xinfo,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata) < 0)
        rc = PMIX_ERROR;
    json_decref (xproc);
    json_decref (xinfo);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    return rc;
}

/**
 ** error callback
 **/

static void recv_error (struct psrv *psrv, const flux_msg_t *msg)
{
    int id;
    int status;
    json_t *xsource;
    json_t *xinfo;
    json_t *xresults;
    json_t *xcbfunc;
    json_t *xcbdata;
    pmix_proc_t source;
    struct infovec *iv = NULL;
    struct infovec *rv = NULL;
    pmix_event_notification_cbfunc_fn_t cbfunc;
    void *cbdata;

    if (flux_msg_unpack (msg,
                         "{s:o s:i}",
                         "id", &id,
                         "status", &status,
                         "source", &xsource,
                         "info", &xinfo,
                         "results", &xresults,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking error notification");
        goto done;
    }
    if (pp_proc_decode (xsource, &source) < 0
        || !(iv = pp_infovec_create_from_json (xinfo))
        || !(rv = pp_infovec_create_from_json (xresults))
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding error notification");
        goto done;
    }
    if (psrv->error_cb) {
        psrv->error_cb (id,
                        status,
                        &source,
                        pp_infovec_info (iv),
                        pp_infovec_count (iv),
                        pp_infovec_info (rv),
                        pp_infovec_count (rv),
                        cbfunc,
                        cbdata);
    }
done:
    pp_infovec_destroy (iv);
}

static void send_error_cb (size_t evhdlr_registration_id,
                           pmix_status_t status,
                           const pmix_proc_t *source,
                           pmix_info_t info[],
                           size_t ninfo,
                           pmix_info_t results[],
                           size_t nresults,
                           pmix_event_notification_cbfunc_fn_t cbfunc,
                           void *cbdata)
{
    struct psrv *psrv = global_server_ctx;
    json_t *xsource = NULL;
    json_t *xinfo = NULL;
    json_t *xresults = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;

    if (!(xsource = pp_proc_encode (source))
        || !(xinfo = pp_info_array_encode (info, ninfo))
        || !(xresults = pp_info_array_encode (results, nresults))
        || !(xcbfunc = pp_pointer_encode (cbfunc))
        || !(xcbdata = pp_pointer_encode (cbdata))
        || pp_socketpair_send_pack (psrv->sp,
                                    "error",
                                    "{s:i s:i s:O s:O s:O s:O s:O}",
                                    "id", evhdlr_registration_id,
                                    "status", status,
                                    "source", xsource,
                                    "info", xinfo,
                                    "results", xresults,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata) < 0)
        log_msg ("pmix: error message dropped");
    json_decref (xsource);
    json_decref (xinfo);
    json_decref (xresults);
    json_decref (xcbfunc);
    json_decref (xcbdata);
}

/**
 ** dmodex_response_cb
 ** callback for PMIx_server_dmodex_request()
 **/

struct dmodex_response {
    pmix_dmodex_response_fn_t cbfunc;
    void *cbdata;
    struct psrv *psrv;
};

static void recv_dmodex_response_cb (struct psrv *psrv, const flux_msg_t *msg)
{
    int status;
    json_t *xdata;
    json_t *xcbfunc;
    json_t *xcbdata;
    void *data = NULL;
    size_t ndata;
    pmix_dmodex_response_fn_t cbfunc;
    void *cbdata;

    if (flux_msg_unpack (msg,
                         "{s:i s:o s:o s:o}",
                         "status", &status,
                         "data", &xdata,
                         "cbfunc", &xcbfunc,
                         "cbdata", &xcbdata) < 0) {
        shell_warn ("error unpacking dmodex_response_cb message");
        return;
    }
    if (pp_data_decode (xdata, &data, &ndata) < 0
        || pp_pointer_decode (xcbfunc, (void **)&cbfunc) < 0
        || pp_pointer_decode (xcbdata, &cbdata) < 0) {
        shell_warn ("error decoding dmodex_response_cb message");
        goto done;
    }
    if (cbfunc)
        cbfunc (status, data, ndata, cbdata);
done:
    free (data);
}

static void send_dmodex_response_cb (pmix_status_t status,
                                     char *data,
                                     size_t size,
                                     void *cbdata)
{
    struct dmodex_response *ctx = cbdata;
    json_t *xdata = NULL;
    json_t *xcbfunc = NULL;
    json_t *xcbdata = NULL;

    if (!(xdata = pp_data_encode (data, size))
        || !(xcbfunc = pp_pointer_encode (ctx->cbfunc))
        || !(xcbdata = pp_pointer_encode (ctx->cbdata))
        || pp_socketpair_send_pack (ctx->psrv->sp,
                                    "dmodex_response_cb",
                                    "{s:i s:O s:O s:O}",
                                    "status", status,
                                    "data", xdata,
                                    "cbfunc", xcbfunc,
                                    "cbdata", xcbdata))
    json_decref (xdata);
    json_decref (xcbfunc);
    json_decref (xcbdata);
    free (ctx);
}

pmix_status_t pp_server_dmodex_request (struct psrv *psrv,
                                        const pmix_proc_t *proc,
                                        pmix_dmodex_response_fn_t cbfunc,
                                        void *cbdata)
{
    struct dmodex_response *ctx;
    int rc;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return PMIX_ERR_NOMEM;
    ctx->cbfunc = cbfunc;
    ctx->cbdata = cbdata;
    ctx->psrv = psrv;
    rc = PMIx_server_dmodex_request (proc, send_dmodex_response_cb, ctx);
    if (rc != PMIX_SUCCESS)
        free (ctx);
    return rc;
}

struct recv_cb recv_callbacks[] = {
    { "client_connected", recv_client_connected },
    { "client_finalized", recv_client_finalized },
    { "client_abort", recv_abort },
    { "fence_nb", recv_fence_nb },
    { "direct_modex", recv_direct_modex },
    { "error", recv_error},
    { "dmodex_response_cb", recv_dmodex_response_cb },
    { NULL, NULL },
};

static pmix_server_module_t send_callbacks = {
    .client_connected = send_client_connected,
    .client_finalized = send_client_finalized,
    .abort = send_abort,
    .fence_nb = send_fence_nb,
    .direct_modex = send_direct_modex,
};

/* A message is received reactively on the shell end of the socket.
 * Dispatch to matched callback from recv_callbacks[] table.
 */
static void recv_cb (const flux_msg_t *msg, void *arg)
{
    struct psrv *psrv = arg;
    const char *topic;
    const char *payload;
    struct recv_cb *cb;

    if (flux_event_decode (msg, &topic, &payload) < 0) {
        shell_warn ("pmix: message decode error - dropped");
        goto done;
    }
    shell_trace ("pmix: callback %s: %s", topic, payload ? payload : "");
    for (cb = &recv_callbacks[0]; cb->name != NULL; cb++) {
        if (streq (topic, cb->name))
            break;
    }
    if (cb->fun)
        cb->fun (psrv, msg);
    else
        shell_warn ("pmix: unhandled callback: %s", topic);
done:
    flux_msg_decref (msg);
}

void pp_server_destroy (struct psrv *psrv)
{
    if (psrv) {
        int saved_errno = errno;
        int rc;

        PMIx_Deregister_event_handler (0, NULL, NULL);
        if ((rc = PMIx_server_finalize ()) != PMIX_SUCCESS)
            shell_warn ("PMIx_server_finalize: %s", PMIx_Error_string (rc));
        pp_socketpair_destroy (psrv->sp);
        global_server_ctx = NULL;
        free (psrv);
        errno = saved_errno;
    }
}

struct psrv *pp_server_create (flux_reactor_t *r,
                               const char *tmpdir,
                               pmix_server_module_t *callbacks,
                               pmix_notification_fn_t error_cb,
                               void *callback_arg)
{
    struct psrv *psrv;
    struct infovec *iv = NULL;
    int rc;

    if (!(psrv = calloc (1, sizeof (*psrv))))
        return NULL;
    psrv->callbacks = callbacks;
    psrv->error_cb = error_cb;
    psrv->callback_arg = callback_arg;

    /* Set up 0MQ push - pull socket pair.
     */
    if (!(psrv->sp = pp_socketpair_create (r))
        || pp_socketpair_recv_register (psrv->sp, recv_cb, psrv) < 0)
        goto error;

    /* Prepare info array to pass to PMIX_server_init
     */
    if (!(iv = pp_infovec_create ()))
        goto error;
    if (pp_infovec_set_str (iv, PMIX_SERVER_TMPDIR, tmpdir) < 0)
        goto error;

    /* Start server thread
     */
    rc = PMIx_server_init (&send_callbacks,
                           pp_infovec_info (iv),
                           pp_infovec_count (iv));
    if (rc != PMIX_SUCCESS) {
        shell_warn ("PMIx_server_init: %s", PMIx_Error_string (rc));
        goto error;
    }

    /* Register error callback
     * Note that the call to Deregister in pp_server_destroy() assumes event
     * handler id=0 since this is the first one registered.
     */
    PMIx_Register_event_handler (NULL, 0, NULL, 0, send_error_cb, NULL, NULL);

    global_server_ctx = psrv;
    pp_infovec_destroy (iv);
    return psrv;
error:
    pp_infovec_destroy (iv);
    pp_server_destroy (psrv);
    return NULL;
}

// vi:tabstop=4 shiftwidth=4 expandtab
