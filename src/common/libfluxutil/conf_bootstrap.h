/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBFLUXUTIL_BOOTSTRAP_H
#define _LIBFLUXUTIL_BOOTSTRAP_H

#include <flux/core.h>
#include <jansson.h>

/* Parse and validate [bootstrap] configuration table.
 * Return 0 on success, -1 on failure with errno set and 'error' filled in.
 * If [bootstrap] is undefined assign NULL to 'hosts' (if provided) and
 * return success.
 *
 * If 'hosts' is non-NULL, assign a copy of the expanded, rank-ordered
 * hosts array, with all the defaults filled in and token substitutions
 * performed.  The caller must release this with json_decref ().
 *
 * 'hostname' should be set to the local hostname.  If bootstrap.hosts is
 * defined one of its entries must match the hostname.  If bootstrap.hosts
 * is not defined, singleton is assumed and 'hosts' will be populated with
 * one {"host"="<hostname>"} entry.
 */
int conf_bootstrap_parse (const flux_conf_t *conf,
                          const char *hostname,
                          bool *enable_ipv6,
                          const char **curve_cert,
                          json_t **hosts,
                          flux_error_t *error);


// internal functions exposed for unit testing
int conf_bootstrap_format_uri (char *buf,
                               int bufsz,
                               const char *fmt,
                               const char *host,
                               int port);
int conf_bootstrap_validate_zmq_uri (const char *uri, flux_error_t *errp);
int conf_bootstrap_validate_domain_name (const char *host, flux_error_t *errp);

#endif // !_LIBFLUXUTIL_BOOTSTRAP_H

// vi:ts=4 sw=4 expandtab

