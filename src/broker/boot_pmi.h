/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_BOOT_PMI_H
#define BROKER_BOOT_PMI_H

/* boot_pmi - bootstrap broker/overlay with PMI */

#include "attr.h"
#include "overlay.h"

int boot_pmi (struct overlay *overlay, attr_t *attrs, int tbon_k);

#endif /* BROKER_BOOT_PMI_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
