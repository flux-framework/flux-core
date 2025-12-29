/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _OVERLAY_BOOT_CONFIG_H
#define _OVERLAY_BOOT_CONFIG_H

/* boot_config - bootstrap broker/overlay from config file */

#include "src/common/libpmi/upmi.h"

#include "attr.h"
#include "overlay.h"

/* Broker attributes read/written directly by this method:
 *   tbon.endpoint (w)
 *   instance-level (w)
 */
int boot_config (struct bootstrap *boot,
                 struct upmi_info *info,
                 flux_t *h,
                 const char *hostname,
                 struct overlay *overlay,
                 attr_t *attrs,
                 flux_error_t *error);

#endif /* _OVERLAY_BOOT_CONFIG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
