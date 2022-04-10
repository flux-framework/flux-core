/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONF_PRIVATE_H
#define _FLUX_CORE_CONF_PRIVATE_H

#include "conf.h"

/* Set errno and optionally 'error' based on glob error return code.
 */
void conf_globerr (flux_error_t *error, const char *pattern, int rc);

#endif /* !_FLUX_CORE_CONF_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
