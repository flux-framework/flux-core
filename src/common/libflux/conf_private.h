/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONF_PRIVATE_H
#define _FLUX_CORE_CONF_PRIVATE_H

#include <stdarg.h>
#include "handle.h"
#include "conf.h"

/* Cache 'conf' in the flux handle for subsequent use by flux_get_conf().
 * Call with conf=NULL to invalidate cache.
 * The config object is destroyed when the handle is destroyed.
 */
int handle_set_conf (flux_t *h, flux_conf_t *conf);

/* Destroy a flux_conf_t
 */
void conf_destroy (flux_conf_t *conf);

/* Parse 'pattern' (a glob) of toml files.
 * If error is non-NULL, it is filled with error details on failure.
 * Caller must destroy returned object with conf_destroy().
 */
flux_conf_t *conf_parse (const char *pattern, flux_conf_error_t *error);

/* Fill 'buf' with glob pattern for Flux config files:
 * If FLUX_CONF_DIR=<directory>, use *.toml in <directory>.
 * If FLUX_CONF_DIR="installed", use *.toml in the installed cf_path.
 * If FLUX_CONF_DIR is unset, use *.toml auto-selected installed/intree cf_path.
 * Returns 0 on success, or -1 with errno=EOVERFLOW if buf is too small.
 */
int conf_get_default_pattern (char *buf, int bufsz);

/* Set errno and optionally 'error' based on glob error return code.
 */
void conf_globerr (flux_conf_error_t *error, const char *pattern, int rc);

#endif /* !_FLUX_CORE_CONF_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
