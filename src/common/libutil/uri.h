/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_URI_H
#define _UTIL_URI_H

#include <flux/core.h>

/*  Resolve a target or "high-level" URI with the flux-uri(1) command,
 *   returning the result. If the URI is already a native Flux URI (e.g.
 *   `local://` or `ssh://`), then `flux uri` is *not* called and instead
 *   the target is returned unmodified to avoid the extra overhead of
 *   running a subprocess.
 *
 *  On failure, NULL is returned. If errp is not NULL, then stderr from
 *   the underlying command will be copied there (possibly truncated).
 *   Otherwise, stderr is not redirected or consumed, so the expectation
 *   is that the underlying `flux uri` error will already be copied to the
 *   callers tty.
 *
 *  Caller must free the returned string on success.
 *
 *  Note: this function uses popen2() to execute flux-uri as a subprocess,
 *   so care should be taken in when and how this function is called.
 */
char *uri_resolve (const char *target, flux_error_t *errp);

/*  Return the authority part of a remote URI, e.g. [username@]host
 *  Returns NULL if uri is NULL or not a remote URI.
 *
 *  Caller must free returned value.
 */
char *uri_remote_get_authority (const char *uri);

#endif /* !_UTIL_URI_H */

// vi:ts=4 sw=4 expandtab
