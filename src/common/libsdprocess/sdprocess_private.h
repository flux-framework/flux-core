/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDPROCESS_PRIVATE_H
#define _SDPROCESS_PRIVATE_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* enable/disable flux_log() for unit tests */
void sdprocess_logging (bool enable);

#ifdef __cplusplus
}
#endif

#endif /* !_SDPROCESS_PRIVATE_H */
