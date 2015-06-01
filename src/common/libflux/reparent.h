#ifndef _FLUX_CORE_REPARENT_H
#define _FLUX_CORE_REPARENT_H

#include <json.h>
#include "handle.h"

char *flux_lspeer (flux_t h, int rank);

int flux_reparent (flux_t h, int rank, const char *uri);

#endif /* !_FLUX_CORE_REPARENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
