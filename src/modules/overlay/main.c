/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/broker/module.h"
#include "overlay.h"

static int mod_main (flux_t *h, int argc, char *argv[])
{
    uint32_t size;
    uint32_t rank;
    const char *hostname;
    const char *broker_uuid;
    const char *broker_boot_method;
    flux_error_t error;
    struct overlay *ov = NULL;
    int rc = -1;

    if (flux_get_size (h, &size) < 0) {
        flux_log_error (h, "getattr size");
        return -1;
    }
    if (flux_get_rank (h, &rank) < 0) {
        flux_log_error (h, "getattr rank");
        return -1;
    }
    if (!(hostname = flux_attr_get (h, "hostname"))) {
        flux_log_error (h, "getattr hostname");
        return -1;
    }
    if (!(broker_uuid = flux_attr_get (h, "broker.uuid"))) {
        flux_log_error (h, "getattr broker.uuid");
        return -1;
    }
    if (!(broker_boot_method = flux_attr_get (h, "broker.boot-method"))) {
        flux_log_error (h, "getattr broker.boot-method");
        return -1;
    }
    if (!(ov = overlay_create (h,
                               rank,
                               size,
                               hostname,
                               broker_uuid,
                               broker_boot_method,
                               NULL,
                               "interthread://overlay",
                               &error))) {
        flux_log (h, LOG_ERR, "%s", error.text);
        return -1;
    }
    if (rank > 0) {
        if (overlay_connect (ov) < 0) {
            flux_log (h, LOG_ERR, "overlay_connect: %s", strerror (errno));
            goto done;
        }
    }
    if (overlay_start (ov) < 0) {
        flux_log (h, LOG_ERR, "overlay_start: %s", strerror (errno));
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    overlay_destroy (ov);
    return rc;
}

struct module_builtin builtin_overlay = {
    .name = "overlay",
    .main = mod_main,
};

// vi:ts=4 sw=4 expandtab
