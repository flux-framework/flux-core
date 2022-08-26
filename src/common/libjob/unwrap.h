/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UNWRAP_STRING
#define _UNWRAP_STRING

#include <flux/core.h>

/*  Unwrap signed data to a NUL terminated string, e.g. J -> jobspec.
 *
 *  If verify is true, then fail if signing mechanism is invalid or
 *   signing user does not match current uid. On failure, error.text
 *   is filled in with an error message. (errno not necessarily
 *   guaranteed to be valid).
 *
 *  Works when flux-core is built with or without flux-security
 *
 *  Caller must free returned value if non-NULL.
 *
 *  flux-core internal use only.
 */
char *unwrap_string (const char *in,
                     bool verify,
                     uint32_t *userid,
                     flux_error_t *error);


/*  As above but use version without flux-security (for testing only)
 */
char *unwrap_string_sign_none (const char *s,
                               bool verify,
                               uint32_t *userid,
                               flux_error_t *errp);

#endif /* !_UNWRAP_STRING */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
