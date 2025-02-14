/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* main.c - module main() for sd-bus bridge module
 *
 * The sdbus module is built when systemd support is not compiled in
 * so that attempts to enable it get helpful error messages instead
 * a generic "not found" error.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"

#if HAVE_LIBSYSTEMD
#include "sdbus.h"
#endif

int mod_main (flux_t *h, int argc, char **argv)
{
#if HAVE_LIBSYSTEMD
    struct sdbus_ctx *ctx = NULL;
    flux_error_t error;
    int rc = -1;

    if (!(ctx = sdbus_ctx_create (h, argc, argv, &error))) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "reactor exited abnormally");
        goto error;
    }
    rc = 0;
error:
    sdbus_ctx_destroy (ctx);
    return rc;
#else
    flux_log (h, LOG_ERR, "flux was not built with systemd support");
    errno = ENOSYS;
    return -1;
#endif
}

// vi:ts=4 sw=4 expandtab
