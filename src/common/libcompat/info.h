#ifndef _FLUX_COMPAT_INFO_H
#define _FLUX_COMPAT_INFO_H

#include <stdbool.h>

#include "src/common/libflux/handle.h"

bool flux_treeroot (flux_t h)
                    __attribute__ ((deprecated));
int flux_rank (flux_t h)
                    __attribute__ ((deprecated));
int flux_size (flux_t h)
                    __attribute__ ((deprecated));
int flux_info (flux_t h, uint32_t *rank, uint32_t *size, int *arity)
                    __attribute__ ((deprecated));

#endif /* !_FLUX_COMPAT_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
