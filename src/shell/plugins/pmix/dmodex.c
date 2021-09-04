/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dmodex.c - call PMIx_server_dmodex_request() remotely
 *
 * Implement a shell service method "pmix-dmodex" that calls
 * the above function and returns the results.
 * The service is registered with pp_dmodex_service_register().
 *
 * The "client" end is wrapped in a function that returns a future,
 * with accessors for the returned data and the status.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/shell.h>
#include <pmix_server.h>

#include "src/common/libutil/errno_safe.h"

#include "internal.h"
#include "task.h"

#include "codec.h"
#include "dmodex.h"

/**
 ** Server
 **/

struct dmodex {
    flux_t *h;
    const flux_msg_t *msg;
};

static void dmodex_destroy (struct dmodex *dm)
{
    if (dm) {
        int saved_errno = errno;
        flux_msg_decref (dm->msg);
        free (dm);
        errno = saved_errno;
    }
}

static struct dmodex *dmodex_create (flux_t *h, const flux_msg_t *msg)
{
    struct dmodex *dm;
    if (!(dm = calloc (1, sizeof (*dm))))
        return NULL;
    dm->h = h;
    dm->msg = flux_msg_incref (msg);
    return dm;
}

// N.B. pmix_dmodex_response_fn_t signature
static void dmodex_response_cb (pmix_status_t status,
                                char *data,
                                size_t ndata,
                                void *cbdata)
{
    struct dmodex *dm = cbdata;
    json_t *xdata;

    if (!(xdata = pp_data_encode (data, ndata))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_respond_pack (dm->h,
                           dm->msg,
                           "{s:i s:O}",
                           "status", status,
                           "data", xdata) < 0)
        shell_warn ("error responding to pmix-dmodex request");
    json_decref (xdata);
    dmodex_destroy (dm);
    return;
error:
    if (flux_respond_error (dm->h, dm->msg, errno, NULL) < 0)
        shell_warn ("error responding to pmix-dmodex request");
    dmodex_destroy (dm);
}

static void dmodex_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct psrv *psrv = arg;
    json_t *xproc;
    pmix_proc_t proc;
    struct dmodex *dm = NULL;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:o}", "proc", &xproc) < 0)
        goto error;
    if (pp_proc_decode (xproc, &proc) < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(dm = dmodex_create (h, msg)))
        goto error;
    rc = pp_server_dmodex_request (psrv, &proc, dmodex_response_cb, dm);
    if (rc != PMIX_SUCCESS) {
        json_t *xdata;
        if (!(xdata = pp_data_encode ("", 0)))
            goto error;
        if (flux_respond_pack (dm->h,
                               dm->msg,
                               "{s:i s:O}",
                               "status", rc,
                               "data", xdata) < 0)
            shell_warn ("error responding to pmix-dmodex request");
        json_decref (xdata);
        dmodex_destroy (dm);
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        goto error;
    dmodex_destroy (dm);
}

int pp_dmodex_service_register (flux_shell_t *shell, struct psrv *psrv)
{
    return flux_shell_service_register (shell,
                                        "pmix-dmodex",
                                        dmodex_request_cb,
                                        psrv);
}

/**
 ** Client
 **/

/* what shell rank hosts proc->rank? */
static int lookup_shell_rank (flux_shell_t *shell, const pmix_proc_t *proc)
{
    int shell_rank;

    for (shell_rank = 0; shell_rank < shell->info->shell_size; shell_rank++) {
        struct rcalc_rankinfo ri;
        if (rcalc_get_nth (shell->info->rcalc, shell_rank, &ri) < 0)
            goto error;
        if (proc->rank >= ri.global_basis
            && proc->rank < ri.global_basis + ri.ntasks)
            return shell_rank;
    }
error:
    errno = ENOENT;
    return -1;
}

flux_future_t *pp_dmodex (flux_shell_t *shell, const pmix_proc_t *proc)
{
    flux_future_t *f;
    int shell_rank;
    json_t *xproc;

    if ((shell_rank = lookup_shell_rank (shell, proc)) < 0)
        return NULL;
    if (!(xproc = pp_proc_encode (proc))) {
        errno = ENOMEM;
        return NULL;
    }
    shell_trace ("pmix-dmodex rpc shell_rank %d proc %d",
                 shell_rank, proc->rank);
    f = flux_shell_rpc_pack (shell,
                             "pmix-dmodex",
                              shell_rank,
                              0,
                              "{s:O}",
                              "proc", xproc);
    ERRNO_SAFE_WRAP (json_decref, xproc);
    return f;
}

int pp_dmodex_get_status (flux_future_t *f, pmix_status_t *status)
{
    if (flux_rpc_get_unpack (f, "{s:i}", "status", status) < 0)
        return -1;
    return 0;

}
int pp_dmodex_get_data (flux_future_t *f, void **datap, int *sizep)
{
    json_t *xdata;
    void *data;
    size_t size;

    if (flux_rpc_get_unpack (f, "{s:o}", "data", &xdata) < 0)
        return -1;
    if (pp_data_decode (xdata, &data, &size) < 0) {
        errno = EPROTO;
        return -1;
    }
    *datap = data;
    *sizep = size;
    return 0;
}

// vi:ts=4 sw=4 expandtab
