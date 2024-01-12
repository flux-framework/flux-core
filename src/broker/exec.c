/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exec.c - broker subprocess server
 *
 * The service is restricted to the instance owner.
 * In addition, remote access to rank 0 is prohibited on multi-user instances.
 * This is a precaution for system instances where rank 0 is deployed on a
 * management node with restricted user access.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libsubprocess/server.h"
#include "src/common/libutil/errprintf.h"

#include "attr.h"
#include "exec.h"
#include "overlay.h"

/* The motivating use case for this was discussed in
 * flux-framework/flux-core#5676
 */
static int is_multiuser_instance (flux_t *h)
{
    int allow_guest_user = 0;
    (void)flux_conf_unpack (flux_get_conf (h),
                            NULL,
                            "{s:{s:b}}",
                            "access",
                              "allow-guest-user", &allow_guest_user);
    return allow_guest_user;
}

static int reject_nonlocal (const flux_msg_t *msg,
                            void *arg,
                            flux_error_t *error)
{
    flux_t *h = arg;
    if (!flux_msg_is_local (msg) && is_multiuser_instance (h)) {
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
    if (!(s = subprocess_server_create (h, "rexec", local_uri, flux_llog, h)))
        goto cleanup;
    if (rank == 0)
        subprocess_server_set_auth_cb (s, reject_nonlocal, h);
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
