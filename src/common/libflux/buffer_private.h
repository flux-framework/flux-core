/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_BUFFER_PRIVATE_H
#define FLUX_BUFFER_PRIVATE_H

#include "buffer.h"

typedef void (*flux_buffer_cb) (flux_buffer_t *fb, void *arg);

/* Call [cb] when the number of bytes stored is greater than [low]
 * bytes.
 *
 * The callback is typically called after an flux_buffer_write() or other
 * write action has occurred on the buffer.  Often, users set [low] to
 * 0, so that the callback is called anytime data has been added to
 * the buffer.
 *
 * At most one callback handler can be set per flux_buffer.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int flux_buffer_set_low_read_cb (flux_buffer_t *fb,
                                 flux_buffer_cb cb,
                                 int low,
                                 void *arg);

/* Call [cb] when a line has been stored.  This callback is typically
 * called after an flux_buffer_write() or other write action has occurred on
 * the buffer.
 *
 * At most one callback handler can be set per flux_buffer.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int flux_buffer_set_read_line_cb (flux_buffer_t *fb,
                                  flux_buffer_cb cb,
                                  void *arg);

/* Call [cb] when the number of bytes stored falls less than
 * [high] bytes.  Setting [cb] to NULL disables the callback.
 *
 * This callback is generally called after a flux_buffer_drop(),
 * flux_buffer_read() or other consumption action has occurred on the buffer.
 * Often, users set [high] to [maxsize], so that the callback is called when the
 * buffer has space for writing.
 *
 * At most one callback handler can be set per flux_buffer.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int flux_buffer_set_high_write_cb (flux_buffer_t *fb,
                                   flux_buffer_cb cb,
                                   int high,
                                   void *arg);

#endif /* !_FLUX_BUFFER_PRIVATE_H */
