#ifndef _FLUX_CORE_BARRIER_H
#define _FLUX_CORE_BARRIER_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a barrier across 'nprocs' processes.
 * The 'name' must be unique across the comms session, or
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
