/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONF_BUILTIN_H
#define _FLUX_CORE_CONF_BUILTIN_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum flux_conf_builtin_hint {
    FLUX_CONF_INSTALLED=0,
    FLUX_CONF_INTREE=1,
    FLUX_CONF_AUTO=2,
};

/* Retrieve builtin (compiled-in) configuration value by name.
 * If hint=INSTALLED, installed paths are used.
 * If hint=INTREE, source/build tree paths are used.
 * If hint=AUTO, a heuristic is employed internally to select paths.
 * This function returns NULL with errno=EINVAL on invalid name.
 */
const char *flux_conf_builtin_get (const char *key,
                                   enum flux_conf_builtin_hint hint);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_CONF_BUILTIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
