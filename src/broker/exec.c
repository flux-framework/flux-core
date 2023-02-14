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
#include <flux/core.h>

#include "src/common/libsubprocess/server.h"
#include "src/common/libutil/errprintf.h"

#include "attr.h"
#include "exec.h"
#include "overlay.h"

static int reject_nonlocal (const flux_msg_t *msg,
                            void *arg,
                            flux_error_t *error)
{
    if (!overlay_msg_is_local (msg)) {
        errprintf (error,
               "Remote rexec requests are not allowed on rank 0");
        return -1;
    }
    return 0;
}

int exec_initialize (flux_t *h, uint32_t rank, attr_t *attrs)
{
    subprocess_server_t *s = NULL;
    const char *local_uri;

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0)
        goto cleanup;
    if (!(s = subprocess_server_create (h, local_uri, rank)))
        goto cleanup;
    if (rank == 0)
        subprocess_server_set_auth_cb (s, reject_nonlocal, NULL);
    if (flux_aux_set (h,
                      "flux::exec",
                      s,
                      (flux_free_f)subprocess_server_destroy) < 0)
        goto cleanup;
    return 0;
cleanup:
    subprocess_server_destroy (s);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
