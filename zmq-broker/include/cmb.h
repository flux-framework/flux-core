#ifndef HAVE_CMB_H
#define HAVE_CMB_H

#include "cmb_socket.h"
#include "flux.h"

/* Create/destroy flux_t handle for this environment
 */
flux_t cmb_init (void);
flux_t cmb_init_full (const char *path, int flags);

#endif /* !HAVE_CMB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
