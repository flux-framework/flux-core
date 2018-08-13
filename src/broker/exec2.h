#ifndef BROKER_EXEC2_H
#define BROKER_EXEC2_H

#include <stdint.h>
#include <flux/core.h>
#include "src/common/subprocess/subprocess.h"
#include "attr.h"

/* Kill any processes started by disconnecting client.
 */
int exec2_terminate_subprocesses_by_uuid (flux_t *h, const char *id);

int exec2_initialize (flux_t *h, uint32_t rank, attr_t *attrs);

#endif /* BROKER_EXEC2_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
