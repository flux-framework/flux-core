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

/*  Like flux_unwrap_string(), but use version without flux-security
 *  (for testing only)
 */
char *unwrap_string_sign_none (const char *s,
                               bool verify,
                               uint32_t *userid,
                               flux_error_t *errp);

#endif /* !_UNWRAP_STRING */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
