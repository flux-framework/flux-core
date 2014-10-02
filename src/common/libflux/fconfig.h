#ifndef _FLUX_CORE_FCONFIG_H
#define _FLUX_CORE_FCONFIG_H

#include <czmq.h>
#include <stdbool.h>

zconfig_t *flux_config_load (const char *path, bool must_exist);
void flux_config_save (const char *path, zconfig_t *z);

#endif /* !_FLUX_CORE_FCONFIG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
