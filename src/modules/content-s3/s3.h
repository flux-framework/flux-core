/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _CONTENT_S3_S3_H
#define _CONTENT_S3_S3_H

/* Configuration info needed for all s3 calls
 */
struct s3_config {
    int retries;        // number of times to retry each operation
    int is_secure;
    int is_virtual_host;
    char *bucket;       // the bucket name for the instance to use
    char *access_key;   // access key id string
    char *secret_key;   // secret access key id string
    char *hostname;     // hostname string
};

/* All int-returning functions below return 0 on success.  On failure,
 * they return -1 with errno set.  In addition, if 'errstr' is non-NULL
 * on failure, it is assigned an S3 status string.
 */

/* Initialize the s3 connection.
 */
int s3_init (struct s3_config *cfg, const char **errstr);

/* Close down the s3 connection.
 */
void s3_cleanup (void);

/* Create a bucket to be used for subsequent put/get operations.
 */
int s3_bucket_create (struct s3_config *cfg, const char **errstr);

/* Write 'data' of length 'size' to object named 'key'.
 */
int s3_put (struct s3_config *cfg,
            const char *key,
            const void *data,
            size_t size,
            const char **errstr);

/* Read 'datap', 'sizep' from object named 'key'.
 */
int s3_get (struct s3_config *cfg,
            const char *key,
            void **datap,
            size_t *sizep,
            const char **errstr);

#endif

/*
 * vi:ts=4 sw=4 expandtab
 */
