/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Define a symbol that can be used to tell the Flux pmi libs from others.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

void *flux_pmi_library;

// vi:ts=4 sw=4 expandtab
