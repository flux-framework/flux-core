/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_FBUF_PRIVATE_H
#define _LIBSUBPROCESS_FBUF_PRIVATE_H

#include "fbuf.h"

typedef void (*fbuf_cb) (struct fbuf *fb, void *arg);

/* Call [cb] when the number of bytes stored is greater than [low]
 * bytes.
 *
 * The callback is typically called after an fbuf_write() or other
 * write action has occurred on the buffer.  Often, users set [low] to
 * 0, so that the callback is called anytime data has been added to
 * the buffer.
 *
 * At most one callback handler can be set per fbuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int fbuf_set_low_read_cb (struct fbuf *fb, fbuf_cb cb, int low, void *arg);

/* Call [cb] when a line has been stored.  This callback is typically
 * called after an fbuf_write() or other write action has occurred on
 * the buffer.
 *
 * At most one callback handler can be set per fbuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int fbuf_set_read_line_cb (struct fbuf *fb, fbuf_cb cb, void *arg);

/* Call [cb] when the number of bytes stored falls less than
 * [high] bytes.  Setting [cb] to NULL disables the callback.
 *
 * This callback is generally called after a fbuf_drop(), fbuf_read()
 * or other consumption action has occurred on the buffer.  Often,
 * users set [high] to [maxsize], so that the callback is called when
 * the buffer has space for writing.
 *
 * At most one callback handler can be set per fbuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int fbuf_set_high_write_cb (struct fbuf *fb, fbuf_cb cb, int high, void *arg);

#endif /* !_LIBSUBPROCESS_FBUF_PRIVATE_H */
