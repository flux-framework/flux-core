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
#include <sys/param.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libsubprocess/command.h"
#include "src/common/libutil/log.h"

#include "attr.h"
#include "exec.h"

static void exec_finalize (void *arg)
{
    flux_subprocess_server_t *s = arg;
    flux_subprocess_server_stop (s);
}

int exec_terminate_subprocesses_by_uuid (flux_t *h, const char *id)
{
    flux_subprocess_server_t *s = flux_aux_get (h, "flux::exec");

    if (!s) {
        flux_log (h, LOG_DEBUG, "no server_ctx found");
        return -1;
    }

    if (flux_subprocess_server_terminate_by_uuid (s, id) < 0) {
        flux_log_error (h, "flux_subprocess_server_terminate_by_uuid");
        return -1;
    }

    return 0;
}

int exec_initialize (flux_t *h, uint32_t rank, attr_t *attrs)
{
    flux_subprocess_server_t *s = NULL;
    const char *local_uri;

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0)
        goto cleanup;
    if (!(s = flux_subprocess_server_start (h, "cmb", local_uri, rank)))
        goto cleanup;
    flux_aux_set (h, "flux::exec", s, exec_finalize);
    return 0;
cleanup:
    flux_subprocess_server_stop (s);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
