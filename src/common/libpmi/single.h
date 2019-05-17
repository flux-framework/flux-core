/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_SINGLE_H
#    define _FLUX_CORE_PMI_SINGLE_H

#    include "src/common/libpmi/pmi_operations.h"

void *pmi_single_create (struct pmi_operations **ops);

#endif /* _FLUX_CORE_PMI_SINGLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
