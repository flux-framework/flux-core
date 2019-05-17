/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_WRAP_H
#    define _FLUX_CORE_PMI_WRAP_H

#    include <stdbool.h>
#    include "src/common/libpmi/pmi_operations.h"

void *pmi_wrap_create (const char *libname,
                       struct pmi_operations **ops,
                       bool allow_self_wrap);

#endif /* _FLUX_CORE_PMI_WRAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
