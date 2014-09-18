#ifndef _FLUX_CORE_MODCTL_H
#define _FLUX_CORE_MODCTL_H

#include <flux/core.h>

int flux_modctl_rm (flux_t h, const char *name);
int flux_modctl_ins (flux_t h, const char *name);
int flux_modctl_update (flux_t h);

#endif /* !_FLUX_CORE_MODCTL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
