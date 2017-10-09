#ifndef _FLUX_CORE_INFO_H
#define _FLUX_CORE_INFO_H

#include "handle.h"

int flux_get_rank (flux_t *h, uint32_t *rank);

int flux_get_size (flux_t *h, uint32_t *size);

const char *flux_get_nodeset (flux_t *h, const char *nodeset,
                              const char *exclude);

#endif /* !_FLUX_CORE_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
