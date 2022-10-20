/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

int main (int argc, char *argv[])
{
    flux_process_scope_t scope;
    const char *str;

    log_init ("flux-scope");

    if (flux_get_process_scope (&scope) < 0)
        log_err_exit ("flux_get_process_scope");

    switch (scope) {
    case FLUX_PROCESS_SCOPE_NONE:
        str = "none";
        break;
    case FLUX_PROCESS_SCOPE_SYSTEM_INSTANCE:
        str = "system instance";
        break;
    case FLUX_PROCESS_SCOPE_INITIAL_PROGRAM:
        str = "initial program";
        break;
    case FLUX_PROCESS_SCOPE_JOB:
        str = "job";
        break;
    default:
        str = "unspecified";
        break;
    }
    printf ("%s\n", str);
    log_fini ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
