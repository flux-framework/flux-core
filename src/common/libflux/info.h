#ifndef _FLUX_CORE_INFO_H
#define _FLUX_CORE_INFO_H

#include <stdbool.h>

#include "handle.h"

int flux_get_rank (flux_t h, uint32_t *rank);

int flux_get_size (flux_t h, uint32_t *size);

int flux_get_arity (flux_t h, int *arity);

#endif /* !_FLUX_CORE_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
