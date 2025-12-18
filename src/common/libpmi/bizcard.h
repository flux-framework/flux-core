/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBPMI_BIZCARD_H
#define LIBPMI_BIZCARD_H

#include <flux/core.h>
#include <jansson.h>

struct bizcard *bizcard_create (const char *hostname, const char *pubkey);
struct bizcard *bizcard_incref (struct bizcard *bc);
void bizcard_decref (struct bizcard *bc);

const char *bizcard_encode (const struct bizcard *bc);
struct bizcard *bizcard_decode (const char *s, flux_error_t *error);

/* N.B. bizcard_get_json() doesn't lend a reference (do not json_decref()!).
 */
struct bizcard *bizcard_fromjson (json_t *obj);
const json_t *bizcard_get_json (const struct bizcard *bc);

int bizcard_uri_append (struct bizcard *bc, const char *uri);
const char *bizcard_uri_first (const struct bizcard *bc);
const char *bizcard_uri_next (const struct bizcard *bc);
const char *bizcard_uri_find (const struct bizcard *bc, const char *scheme);

const char *bizcard_pubkey (const struct bizcard *bc);
const char *bizcard_hostname (const struct bizcard *bc);

#endif /* LIBPMI_BIZCARD_H */

// vi:ts=4 sw=4 expandtab
