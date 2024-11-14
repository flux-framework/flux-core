/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_BARRIER_H
#define _FLUX_CORE_BARRIER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a barrier across 'nprocs' processes.
 * The 'name' must be unique across the Flux instance, or
 * if running in a Flux/slurm job, may be NULL.
 */
flux_future_t *flux_barrier (flux_t *h, const char *name, int nprocs);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_BARRIER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
