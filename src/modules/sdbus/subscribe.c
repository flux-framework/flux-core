/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* subscribe.c - composite RPC for Subscribe and AddMatch
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "subscribe.h"

static const char *match_signal_all = "type=signal";


static void subscribe_continuation (flux_future_t *f1, void *arg)
{
    flux_t *h = flux_future_get_flux (f1);
    const char *errmsg = NULL;
    flux_future_t *f2;
    const char *name = flux_aux_get (h, "flux::name");
    char topic[128];

    if (flux_rpc_get (f1, NULL) < 0) {
        errmsg = future_strerror (f1, errno);
        goto error;
    }
    snprintf (topic, sizeof (topic), "%s.call", name);
    if (!(f2 = flux_rpc_pack (h,
                              topic,
                              FLUX_NODEID_ANY,
                              0,
                              "{s:s s:s s:s s:s s:[s]}",
                              "destination", "org.freedesktop.DBus",
                              "path", "/org/freedesktop/DBus",
                              "interface", "org.freedesktop.DBus",
                              "member", "AddMatch",
                              "params", match_signal_all))
        || flux_future_continue (f1, f2) < 0) {
        errmsg = "error continuing subscribe request";
        flux_future_destroy (f2);
        goto error;
    }
    goto done;
error:
    flux_future_continue_error (f1, errno, errmsg);
done:
    flux_future_destroy (f1);
}

flux_future_t *sdbus_subscribe (flux_t *h)
{
    flux_future_t *f1;
    flux_future_t *fc;
    const char *name = flux_aux_get (h, "flux::name");
    char topic[128];

    snprintf (topic, sizeof (topic), "%s.call", name);
    if (!(f1 = flux_rpc_pack (h,
                              topic,
                              FLUX_NODEID_ANY,
                              0,
                              "{s:s s:[]}",
                              "member", "Subscribe",
                              "params"))
        || !(fc = flux_future_and_then (f1, subscribe_continuation, NULL))) {
        flux_future_destroy (f1);
        return NULL;
    }
    return fc;
}

// vi:tabstop=4 shiftwidth=4 expandtab
