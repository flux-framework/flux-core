/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PANIC_H
#    define _FLUX_CORE_PANIC_H

#    include <stdint.h>
#    include "handle.h"

#    ifdef __cplusplus
extern "C" {
#    endif

/* Tell broker on 'nodeid' to call _exit() after displaying 'reason'
 * on stderr.  'nodeid' may be FLUX_NODEID_ANY to select the local
 * broker.  Currently flags should be set to zero.
 */
int flux_panic (flux_t *h, uint32_t nodeid, int flags, const char *reason);

#    ifdef __cplusplus
}
#    endif

#endif /* !_FLUX_CORE_PANIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
