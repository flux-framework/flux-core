#ifndef _FLUX_CORE_INFO_H
#define _FLUX_CORE_INFO_H

#include <stdbool.h>

#include "handle.h"

int flux_info (flux_t h, uint32_t *rankp, uint32_t *sizep, int *arityp);
int flux_rank (flux_t h);
int flux_size (flux_t h);

#endif /* !_FLUX_CORE_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
