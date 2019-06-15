/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "src/common/libutil/log.h"
#include "src/common/libsubprocess/subprocess.h"

#include "procs.h"

struct shell_procs {
};

struct shell_procs *shell_procs_create (flux_t *h, struct shell_info *info)
{
    struct shell_procs *procs;

    if (!(procs = calloc (1, sizeof (*procs)))) {
        log_err ("shell_procs_create");
        return NULL;
    }
    return procs;
}

void shell_procs_destroy (struct shell_procs *procs)
{
    if (procs) {
        int saved_errno = errno;
        free (procs);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
