/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_FBUF_WATCHER_H
#define _LIBSUBPROCESS_FBUF_WATCHER_H

#include <flux/core.h>
#include "fbuf.h"

enum {
    FBUF_WATCHER_LINE_BUFFER = 1, /* line buffer data before invoking callback */
};

/* read watcher
 *
 * - data from fd copied into buffer
 * - when data is available, triggers callback (FLUX_POLLIN)
 * - on eof, callback will be called with an empty buffer (FLUX_POLLIN)
 * - if line buffered, second to last callback may not contain a full line
 * - users should read from the buffer or stop the watcher, to avoid
 *   excessive event loop iterations without progress
 */
flux_watcher_t *fbuf_read_watcher_create (flux_reactor_t *r,
                                          int fd,
                                          int size,
                                          flux_watcher_f cb,
                                          int flags,
                                          void *arg);

struct fbuf *fbuf_read_watcher_get_buffer (flux_watcher_t *w);

/* Get next chunk of data from a buffered read watcher. Gets the next
 * line if the watcher is line buffered.
 */
const char *fbuf_read_watcher_get_data (flux_watcher_t *w, int *lenp);

/* Take a reference on read watcher to prevent read of EOF
 * EOF will be delayed until decref drops refcount to 0.
 */
void fbuf_read_watcher_incref (flux_watcher_t *w);
void fbuf_read_watcher_decref (flux_watcher_t *w);

/* write watcher
 *
 * - data from buffer written to fd
 * - callback triggered after:
 *   - fbuf_write_watcher_close() was called AND any buffered data has
 *     been written out (FLUX_POLLOUT)
 *   - error (FLUX_POLLERR)
 */
flux_watcher_t *fbuf_write_watcher_create (flux_reactor_t *r,
                                           int fd,
                                           int size,
                                           flux_watcher_f cb,
                                           int flags,
                                           void *arg);

struct fbuf *fbuf_write_watcher_get_buffer (flux_watcher_t *w);

/* "write" EOF to buffer write watcher 'w'. The underlying fd will be closed
 *  once the buffer is emptied. The underlying struct fbuf will be marked
 *  readonly and subsequent fbuf_write* calls will return EROFS.
 *
 *  Once close(2) completes, the watcher callback is called with FLUX_POLLOUT.
 *  Use fbuf_write_watcher_is_closed() to check for errors.
 *
 * Returns 0 on success, -1 on error with errno set.
 */
int fbuf_write_watcher_close (flux_watcher_t *w);

/* Returns 1 if write watcher is closed, errnum from close in close_err */
int fbuf_write_watcher_is_closed (flux_watcher_t *w, int *close_err);

#endif /* !_LIBSUBPROCESS_FBUF_WATCHER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
