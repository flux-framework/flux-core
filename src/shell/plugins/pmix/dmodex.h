/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PMIX_PP_DIRECT_H
#define _PMIX_PP_DIRECT_H

#include <flux/core.h>
#include <flux/shell.h>
#include <pmix_server.h>

#include "server.h"

flux_future_t *pp_dmodex (flux_shell_t *shell, const pmix_proc_t *proc);

int pp_dmodex_get_status (flux_future_t *f, pmix_status_t *status);
// caller must free data
int pp_dmodex_get_data (flux_future_t *f, void **data, int *size);

int pp_dmodex_service_register (flux_shell_t *shell, struct psrv *psrv);

#endif

// vi:ts=4 sw=4 expandtab
