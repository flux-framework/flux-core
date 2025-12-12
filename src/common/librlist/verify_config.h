/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_VERIFY_CONFIG_H
#define HAVE_VERIFY_CONFIG_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libflux/types.h" /* flux_error_t */

/* Supported per-resource verify modes:
 */
enum rlist_verify_mode {
    RLIST_VERIFY_STRICT = 1,    // "strict"
    RLIST_VERIFY_ALLOW_MISSING, // "allow-missing"
    RLIST_VERIFY_ALLOW_EXTRA,   // "allow-extra"
    RLIST_VERIFY_IGNORE,        // "ignore"
};

struct rlist_verify_config;

/*  Parse a `resource.verify` config object in `config` and return an
 *   rlist_verify_config object.
 *
 *  Example config:
 *
 *    [resource.verify]
 *    default = "allow-extra"  # allow extra resources by default
 *    hostname = "strict"      # strict hostname checking
 *
 *  Allowed modes are "strict", "allow-extra", "allow-missing", "ignore"
 *  Allowed resource types are "core", "gpu", "hostname"
 *
 *  As a convenience, `config` may also be a boolean, where true is equivalent
 *  to `default="strict"` and false `default="ignore"`.
 *
 */
struct rlist_verify_config *rlist_verify_config_create (json_t *config,
                                                        flux_error_t *errp);

void rlist_verify_config_destroy (struct rlist_verify_config *config);

/*  Update an existing resource verify config object
 */
int rlist_verify_config_update (struct rlist_verify_config *conf,
                                json_t *config,
                                flux_error_t *errp);


/*  Return the verify mode in `config` for resource 'type'.
 *  Returns "strict" by default, i.e. config == NULL.
 */
enum rlist_verify_mode
rlist_verify_config_get_mode (const struct rlist_verify_config *config,
                              const char *type);

/*  Update the verify mode for resource `type` to `mode`.
 *  Returns 0 on success, -1 with errno set for error.
 *  Errors:
 *   EINVAL - `config` or `type` were invalid.
 */
int rlist_verify_config_set_mode (struct rlist_verify_config *config,
                                  const char *type,
                                  enum rlist_verify_mode mode);


/*  Return true if the verify config was created from an explicit
 *  configuration (non-NULL JSON object or `true`), false if it
 *  was created with default settings only.
 */
bool rlist_verify_config_is_explicit (const struct rlist_verify_config *config);

#endif /* !HAVE_VERIFY_CONFIG_H */
