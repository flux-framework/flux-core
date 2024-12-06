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

struct blobvec_param {
    const char *hashtype;
    size_t chunksize;              // maximum size of each blob
    size_t small_file_threshold;   // no blobvec encoding for regular files of
};                                 //  size <= thresh (0=always blobvec)

struct blobvec_mapinfo {
    void *base;
    size_t size;
};


/* Variant of fileref_create() with extra parameters to allow for 'blobvec'
 * encoding.
 * - If 'param' is non-NULL, blobvec encoding is enabled with the specified
 *   params.
 * - If 'mapinfo' is non-NULL, and the file meets conditions for blobvec
 *   encoding, the file remains mapped in memory and its address is returned.
 */
json_t *fileref_create_ex (const char *path,
                           struct blobvec_param *param,
                           struct blobvec_mapinfo *mapinfo,
                           flux_error_t *error);

/* Create a fileref object for the file system object at 'path'.
 * The blobvec encoding is never used thus the object is self-contained.
 */
json_t *fileref_create (const char *path, flux_error_t *error);

/* Build a "directory listing" of a fileref and set it in 'buf'.
 * Set 'path' if provided from archive container (fileref->path overrides).
 * If the fileref is invalid, set "invalid fileref".
 * If output is truncated, '+' is substituted for the last character.
 */
void fileref_pretty_print (json_t *fileref,
                           const char *path,
                           bool long_form,
                           char *buf,
                           size_t bufsize);

#endif /* !_UTIL_FILEREF_H */

// vi:ts=4 sw=4 expandtab
