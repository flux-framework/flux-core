/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _IOBUF_H
#define _IOBUF_H

#include <sys/types.h>

#include <flux/core.h>

enum iobuf_flags {
    IOBUF_FLAG_LOG_ERRORS = 1,
};

struct iobuf_data {
    const char *stream;
    int rank;
    const char *data;
    int data_len;
};

typedef struct iobuf iobuf_t;

typedef void (*iobuf_f) (iobuf_t *iob, void *arg);

/* Create iobuf server with service name 'service_name'.  Set
 * 'max_count' to maximum stream/rank combinations allowed.  Set
 * 'max_count' to 0 for no max.
 *
 * User is responsible for calling `flux_service_register` and
 * `flux_service_unregister` for `service_name`.
 */
iobuf_t *iobuf_server_create (flux_t *h,
                              const char *service_name,
                              int max_count,
                              int flags);

void iobuf_server_destroy (iobuf_t *iob);

/* Set a callback to be called after 'eof_count' buffers have been
 * EOFed.  Typically 'eof_count' is set to same 'max_count' in
 * iobuf_server_create().  Callback is called at most once.
 * - Returns 0 on success, -1 on error
 */
int iobuf_set_eof_count_cb (iobuf_t *iob,
                            int eof_count,
                            iobuf_f eof_count_cb,
                            void *arg);

/* Create a stream/rank buffer combination in the iobuf service.  It
 * is not necessary to call this as the first call to iobuf_write() or
 * iobuf_eof() will do this as well.  This is useful if user wishes
 * "pre-setup" specific stream/rank combinations up to the
 * `max_buffer_count` before using the service.
 * - Returns 0 on success, -1 on error
 */
int iobuf_create (iobuf_t *iob,
                  const char *stream,
                  int data_rank);

/* Write to the iobuf service for the stream/rank.
 * - Returns 0 on success, -1 on error
 */
int iobuf_write (iobuf_t *iob,
                 const char *stream,
                 int data_rank,
                 const char *data,
                 int data_len);

/* Mark the stream/rank combination as EOFed/complete.  No further
 * writes can occur on the stream/rank.
 * - Returns 0 on success, -1 on error
 */
int iobuf_eof (iobuf_t *iob,
               const char *stream,
               int data_rank);

/* Read data stored in iobuf for the stream/rank.
 * - Caller is required to free data result
 * - If no data stored, 'data' will be set to NULL
 * - Returns 0 on success, -1 on error
 */
int iobuf_read (iobuf_t *iob,
                const char *stream,
                int data_rank,
                char **data,
                int *data_len);

/* Iterate through all writes to the iobuf service
 * - Caller will not free results
 */
const struct iobuf_data *iobuf_iter_first (iobuf_t *iob);
const struct iobuf_data *iobuf_iter_next (iobuf_t *iob);

/* Get number of stream/ranks created */
int iobuf_count (iobuf_t *iob);
/* Get number of stream/ranks that have been EOFed in service */
int iobuf_eof_count (iobuf_t *iob);

/*
 * RPC equivalent functions to above, returns future on success, NULL
 * on error.
 */

flux_future_t *iobuf_rpc_create (flux_t *h,
                                 const char *name,
                                 int rpc_rank,
                                 const char *stream,
                                 int data_rank);

flux_future_t *iobuf_rpc_write (flux_t *h,
                                const char *name,
                                int rpc_rank,
                                const char *stream,
                                int data_rank,
                                const char *data,
                                int data_len);

flux_future_t *iobuf_rpc_eof (flux_t *h,
                              const char *name,
                              int rpc_rank,
                              const char *stream,
                              int data_rank);

flux_future_t *iobuf_rpc_read (flux_t *h,
                               const char *name,
                               int rpc_rank,
                               const char *stream,
                               int data_rank);

/* Returns 0 on success, -1 on error.  Caller is required to free data
 * result */
int iobuf_rpc_read_get (flux_future_t *f, char **data, int *data_len);

#endif /* !_IOBUF_H */
