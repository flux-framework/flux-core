/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"
#include "src/broker/module.h"

bool flux_module_debug_test (flux_t *h, int flag, bool clear)
{
    int *flagsp = flux_aux_get (h, "flux::debug_flags");

    if (!flagsp || !(*flagsp & flag))
        return false;
    if (clear)
        *flagsp &= ~flag;
    return true;
}

int flux_module_set_running (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "module.status",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i}",
                             "status", FLUX_MODSTATE_RUNNING)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

int flux_module_config_request_decode (const flux_msg_t *msg,
                                       flux_conf_t **confp)
{
    json_t *o;
    flux_conf_t *conf;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0
        || !(conf = flux_conf_pack ("O", o)))
        return -1;
    if (confp)
        *confp = conf;
    else
        flux_conf_decref (conf);
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
