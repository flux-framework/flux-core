#ifndef _FLUX_COMPAT_INFO_H
#define _FLUX_COMPAT_INFO_H

#include <stdbool.h>

#include "src/common/libflux/handle.h"

bool flux_treeroot (flux_t h)
                    __attribute__ ((deprecated));

#endif /* !_FLUX_COMPAT_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
