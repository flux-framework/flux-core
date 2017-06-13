#ifndef _FLUX_CORE_BARRIER_H
#define _FLUX_CORE_BARRIER_H

#include <flux/core.h>

/* Execute a barrier across 'nprocs' processes.
 * The 'name' must be unique across the comms session, or
 * if running in a Flux LWJ, may be NULL.
 */
int flux_barrier (flux_t *h, const char *name, int nprocs);

#endif /* !_FLUX_BARRIER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
