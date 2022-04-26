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

/* Initialize the s3 connection.
 * On success, an s3 connection to the endpoint is created
 * and S3StatusOK (0) is returned. On failure, a status
 * > 0 will be returned.
 */
int s3_init (struct s3_config *cfg, const char **errstr);

/* Close down the s3 connection.
 */
void s3_cleanup (void);

/* Create a bucket on the s3 connection.
 * On success, a bucket will be created and 0 will be returned.
 * On failure, a -1 will be reuturned and a bucket
 * will not be created.
 */
int s3_bucket_create (struct s3_config *cfg, const char **errstr);

/* Put an object to the s3 bucket.
 * On success, the data will be put into the bucket
 * and 0 will be returned. On failure, the data will not put
 * to the bucket and -1 will be returned.
 */
int s3_put (struct s3_config *cfg,
            const char *key,
            const void *data,
            size_t size,
            const char **errstr);

/* Get an object's data from the s3 bucket.
 * On success, the object with key 'key' will be retrieved
 * from the bucket and the data written to 'datap'
 * and 0 will be returned. On failure, the data will not
 * be retrieved from the bucket and -1 will be returned.
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
