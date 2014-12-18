#ifndef _FLUX_CORE_MODCTL_H
#define _FLUX_CORE_MODCTL_H

#include <stdint.h>
#include <flux/core.h>

int flux_modctl_list (flux_t h, const char *svc, const char *nodeset,
                      flux_lsmod_f cb, void *arg);
int flux_modctl_load (flux_t h, const char *nodeset,
                      const char *path, int argc, char **argv);
int flux_modctl_unload (flux_t h, const char *nodeset, const char *name);

#endif /* !_FLUX_CORE_MODCTL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
