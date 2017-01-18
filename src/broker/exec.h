#ifndef BROKER_EXEC_H
#define BROKER_EXEC_H

#include <stdint.h>
#include <flux/core.h>
#include "src/common/libsubprocess/subprocess.h"
#include "attr.h"

/* Kill any processes started by disconnecting client.
 */
int exec_terminate_subprocesses_by_uuid (flux_t *h, const char *id);

int exec_initialize (flux_t *h, struct subprocess_manager *sm,
                     uint32_t rank, attr_t *attrs);

#endif /* BROKER_EXEC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
