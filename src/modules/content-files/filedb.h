/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _CONTENT_FILES_FILEDB_H
#define _CONTENT_FILES_FILEDB_H

/* Read file named 'key' from the dbpath directory.
 * On success, 'datap' and 'sizep' are assigned the contents and size
 * and 0 is returned (*datap must be freed).
 * On failure, -1 is returned with errno set.
 * Pass '*errstr' (pre-set to NULL) and if a human readable error message
 * is appropriate, it is assigned on error (do not free).
 */
int filedb_get (const char *dbpath,
                const char *key,
                void **datap,
                size_t *sizep,
                const char **errstr);


/* Put file named 'key' with content 'data' and length 'size' to the
 * dbpath directory.  On success, 0 is returned.
 * On failure, -1 is returned with errno set.
 * Pass '*errstr' (pre-set to NULL) and if a human readable error message
 * is appropriate, it is assigned on error (do not free).
 */
int filedb_put (const char *dbpath,
                const char *key,
                const void *data,
                size_t size,
                const char **errstr);

#endif /* !_CONTENT_FILES_FILEDB_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
