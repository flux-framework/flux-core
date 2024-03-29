/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_FILEMAP_H
#define HAVE_FLUX_FILEMAP_H

#include <stdarg.h>

#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"

/*
 *  Tracing callback for filemap_extract()
 */
typedef void (*filemap_trace_f) (void *arg,
                                 json_t *fileref,
                                 const char *path,
                                 int mode,
                                 int64_t size,
                                 int64_t mtime,
                                 int64_t ctime,
                                 const char *encoding);

/*  Extract an RFC 37 File Archive in either array or dictionary form.
 *  If 'trace_cb' is set, then it will be called for each extracted file.
 *
 *  Returns 0 on success, or -1 with error set in errp when non-NULL.
 *
 */
int filemap_extract (flux_t *h,
                     json_t *files,
		     int libarchive_flags,
                     flux_error_t *errp,
                     filemap_trace_f trace_cb,
                     void *arg);

#endif /* !HAVE_FLUX_FILEMAP_H */
