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

#include "attr.h"
#include "overlay.h"
#include "bootstrap.h"

#include "src/common/libpmi/upmi.h"

int boot_pmi (struct bootstrap *boot,
              struct upmi_info *info,
              const char *hostname,
              struct overlay *overlay,
              attr_t *attrs,
              flux_error_t *error);

#endif /* _OVERLAY_BOOT_PMI_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
