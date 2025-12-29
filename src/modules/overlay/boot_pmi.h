/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _OVERLAY_BOOT_PMI_H
#define _OVERLAY_BOOT_PMI_H

/* boot_pmi - bootstrap broker/overlay with PMI */

#include "overlay.h"

#include "src/common/libpmi/upmi.h"

int boot_pmi (flux_t *h,
              uint32_t rank,
              uint32_t size,
              const char *hostname,
              struct overlay *overlay,
              flux_error_t *error);

#endif /* _OVERLAY_BOOT_PMI_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
