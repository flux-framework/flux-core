/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* alloc request handling
 *
 * Handle 'shell-<id>.dyn_alloc_request' by forwarding request to job manager
 */
#define FLUX_SHELL_PLUGIN_NAME "alloc_request_handler"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "builtins.h"

#include <jansson.h>


struct alloc_ctx {
    flux_t *h;
    const flux_msg_t *msg;
};

/* receives RESPONSE from job manager for dynamic allocation request. 
 * forwards RESPONSE to client/pmix_plugin  
 */
static void dyn_alloc_response_cb (flux_future_t *f, void *arg)
{
    json_t *xdata = NULL;
    struct alloc_ctx *ctx = arg;
    int rc;

    shell_debug("received allocation response from job-manager");

    /* TODO: DO whatever is necessary for LEVEL 'shell' before forwarding response to pmix_plugin */

    if (flux_rpc_get_unpack (f, "{s:O}", "data", &xdata) < 0){
        shell_warn ("pmix-alloc request: %s", future_strerror (f, errno));
        flux_respond_error(ctx->h, ctx->msg, -1, "unable to unpack response");
        goto out;
    }

    if(0 > (rc = flux_respond_pack(ctx->h, ctx->msg,  "{s:O}", "data", xdata))){
        shell_warn("flux_respond_pack failed with %d", rc);
        flux_respond_error(ctx->h, ctx->msg, -1, "unable to pack response");
        goto out;
    };
    shell_debug("forwarded allocation response");

out:
    flux_msg_decref(ctx->msg);
    free(ctx);
}

/* recives REQUEST from client/pmix_plugin for dynamic allocation request. 
 * forwards REQUEST to job manager  
 */
static void dyn_alloc_request_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    //json_t *xproc;
    //pmix_proc_t proc;
    json_t *payload;
    int directive;
    flux_future_t *f;
    shell_debug ("received allocation request");

   
    /* TODO: DO whatever is necessary for LEVEL 'shell' before forwarding request to job manager */



    struct alloc_ctx *ctx = calloc(1, sizeof(*ctx));
    ctx->h = h;
    ctx->msg = flux_msg_incref(msg);


    if(0 < flux_msg_unpack (msg,
                         "{s:i s:O}",
                         //"proc", &xproc,
                         "directive", &directive,
                         "data", &payload)){
        
        shell_warn ("error unpacking interthread alloc_upcall message");
        return;
    }
    /* Send alloc request to job manager */
    if( !(f = flux_rpc_pack(h, 
                "job-manager.dyn_alloc_request", 
                FLUX_NODEID_ANY, 
                0, 
                "{s:i s:O}",
                "directive", directive,
                "data", payload )) 
        || flux_future_then (f,
                             -1,
                             dyn_alloc_response_cb,
                             (void *) ctx) < 0) 
    {
        if(f) flux_future_destroy (f);
    }
    shell_debug ("forward allocation request to job-manager");

}

static int alloc_request_init (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!shell)
        return -1;
    if (flux_shell_service_register (shell, "dyn_alloc_request", dyn_alloc_request_cb, shell) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_alloc_reuest = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = alloc_request_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
