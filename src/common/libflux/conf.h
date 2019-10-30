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

#include <stdarg.h>
#include "handle.h"

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


typedef struct flux_conf flux_conf_t;

typedef struct {
    char filename[80];  // if unused, will be set to empty string
    int lineno;         // if unused, will be set to -1
    char errbuf[160];
} flux_conf_error_t;

/* Parse Flux's config files (*.toml in cf_path).
 *
 * The builtin value for cf_path (intree|installed selected automatically) is
 * used unless overridden with the FLUX_CONF_DIR environment variable.
 * FLUX_CONF_DIR may be set to a directory path or may be set to the
 * special value "installed" to force use of the installed cf_path.
 *
 * On success, return the object, saving the result for reuse on future calls.
 * On failure, return NULL, set errno, and (if non-NULL) fill 'error' with
 * details about the failure.
 *
 * The config object is destroyed when the handle is destroyed.
 */
const flux_conf_t *flux_get_conf (flux_t *h, flux_conf_error_t *error);

/* Access config object.
 * If error is non-NULL, it is filled with error details on failure.
 */
int flux_conf_vunpack (const flux_conf_t *conf,
                       flux_conf_error_t *error,
                       const char *fmt,
                       va_list ap);

int flux_conf_unpack (const flux_conf_t *conf,
                      flux_conf_error_t *error,
                      const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_CONF_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
