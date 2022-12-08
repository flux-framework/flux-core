/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_FILEREF_H
#define _UTIL_FILEREF_H

#include <sys/types.h>
#include <sys/stat.h>
#include <jansson.h>

#include "src/common/libflux/types.h"

/* Variant of fileref_create() with extra parameters:
 * - If non-NULL, 'mapbuf' and 'mapsize' are assigned mmapped buffer
 *   (if applicable)
 * - If non-NULL 'fullpath' is used in place of 'path' for open/stat/mmap.
 */
json_t *fileref_create_ex (const char *path,
                           const char *fullpath,
                           const char *hashtype,
                           int chunksize,
                           void **mapbuf,
                           size_t *mapsize,
                           flux_error_t *error);

/* Create a fileref object for the regular file at 'path'.
 * The chunksize limits the size of each blob (0=unlimited).
 * Corner cases handled: empty files and sparse files.
 */
json_t *fileref_create (const char *path,
                        const char *hashtype,
                        int chunksize,
                        flux_error_t *error);

/* Build a "directory listing" of a fileref and set it in 'buf'.
 * If the fileref is invalid, set "invalid fileref".
 * If output is truncated, '+' is substituted for the last character.
 */
void fileref_pretty_print (json_t *fileref,
                           bool long_form,
                           char *buf,
                           size_t bufsize);

#endif /* !_UTIL_FILEREF_H */

// vi:ts=4 sw=4 expandtab
