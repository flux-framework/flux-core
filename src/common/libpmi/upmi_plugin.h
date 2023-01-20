/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBPMI_UPMI_PLUGIN_H
#define _LIBPMI_UPMI_PLUGIN_H 1

#include <stdarg.h>
#include <flux/core.h>

int upmi_seterror (flux_plugin_t *p,
                   flux_plugin_arg_t *args,
                   const char *fmt,
                   ...)
__attribute__ ((format (printf, 3, 4)));


#endif /* !_LIBPMI_UPMI_PLUGIN_H */

// vi:ts=4 sw=4 expandtab
