#ifndef BROKER_LOG_H
#define BROKER_LOG_H

#include <flux/core.h>
#include "attr.h"

int logbuf_initialize (flux_t *h, uint32_t rank, attr_t *attrs);

#endif /* BROKER_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
