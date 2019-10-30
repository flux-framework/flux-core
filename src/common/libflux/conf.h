/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONF_H
#define _FLUX_CORE_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

enum flux_conf_flags {
    FLUX_CONF_INSTALLED=0,
    FLUX_CONF_INTREE=1,
    FLUX_CONF_AUTO=2,
};

/* Retrieve builtin (compiled-in) configuration value by name.
 * If flags=INSTALLED, installed paths are used.
 * If flags=INTREE, source/build tree paths are used.
 * If flags=AUTO, a heuristic is employed internally to select paths.
 * This function returns NULL with errno=EINVAL on invalid name.
 */
const char *flux_conf_builtin_get (const char *name,
                                   enum flux_conf_flags flags);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_CONF_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
