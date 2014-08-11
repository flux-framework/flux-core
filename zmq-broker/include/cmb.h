#ifndef FLUX_CMB_H
#define FLUX_CMB_H

#include "flux.h"

/* Create/destroy flux_t handle for the API environment.
 * In this environment, all communication with cmbd is over a UNIX
 * domain socket.
 */
flux_t cmb_init (void);
flux_t cmb_init_full (const char *path, int flags);

#endif /* !FLUX_CMB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
