/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_FBUF_H
#define _LIBSUBPROCESS_FBUF_H

#include <stdbool.h>

/* Create buffer.
 */
struct fbuf *fbuf_create (int size);

void fbuf_destroy (void *fb);

/* Returns the buffer size, set when fbuf_create () was called */
int fbuf_size (struct fbuf *fb);

/* Returns the number of bytes current stored in fbuf */
int fbuf_bytes (struct fbuf *fb);

/* Returns the number of bytes of space available in fbuf */
int fbuf_space (struct fbuf *fb);

/* Manage "readonly" status
 * - fbuf_readonly() makes it so writes are no longer allowed
 *   to the buffer.  Reads are allowed until the buffer is empty.
 *   Changing a buffer to "readonly" can only be called once and
 *   cannot be disabled.  This is a convenience status can be used to
 *   indicate to users that the buffer is no longer usable.
 * - fbuf_is_readonly() returns true if a buffer is readonly,
 *    and false if not.
 */
int fbuf_readonly (struct fbuf *fb);
bool fbuf_is_readonly (struct fbuf *fb);

/* Drop up to [len] bytes of data in the buffer. Set [len] to -1
 * to drop all data.  Returns number of bytes dropped on success.
 */
int fbuf_drop (struct fbuf *fb, int len);

/* Read up to [len] bytes of data in the buffer without consuming it.
 * Pointer to buffer is returned to user and optionally length read
 * can be returned to user in [lenp].  The buffer will always be NUL
 * terminated, so the user may treat returned ptr as a string.  User
 * shall not free returned pointer.  If no data is available, returns
 * pointer and length of 0.  Set [len] to -1 to read all data.
 */
const void *fbuf_peek (struct fbuf *fb, int len, int *lenp);

/* Read up to [len] bytes of data in the buffer and mark data as
 * consumed.  Pointer to buffer is returned to user and optionally
 * length read can be returned to user in [lenp].  The buffer will
 * always be NUL terminated, so the user may treat returned ptr as a
 * string.  User shall not free returned pointer.  If no data is
 * available, returns pointer and length of 0.  Set [len] to -1 to
 * read all data.
 */
const void *fbuf_read (struct fbuf *fb, int len, int *lenp);

/* Write [len] bytes of data into the buffer.  Returns number of bytes
 * written on success.
 */
int fbuf_write (struct fbuf *fb, const void *data, int len);

/* Determines lines available for peeking/reading.  Returns -1
 * on error, >= 0 for lines available */
int fbuf_lines (struct fbuf *fb);

/* Return true if buffer has at least one unread line */
bool fbuf_has_line (struct fbuf *fb);

/* Drop a line in the buffer.  Returns number of bytes dropped on
 * success. */
int fbuf_drop_line (struct fbuf *fb);

/* Read a line in the buffer without consuming it.  Return buffer will
 * include newline.  Optionally return length of data returned in
 * [lenp].  If no line is available, returns pointer and length of
 * 0. Return NULL on error.
 */
const void *fbuf_peek_line (struct fbuf *fb, int *lenp);

/* Identical to fbuf_peek_line(), but does not return trailing
 * newline */
const void *fbuf_peek_trimmed_line (struct fbuf *fb, int *lenp);

/* Read a line in the buffer and mark data as consumed.  Return buffer
 * will include newline.  Optionally return length of data returned in
 * [lenp].  If no line is available, returns pointer and length of 0.
 * Return NULL on error.
 */
const void *fbuf_read_line (struct fbuf *fb, int *lenp);

/* Identical to fbuf_read_line(), but does not return trailing
 * newline */
const void *fbuf_read_trimmed_line (struct fbuf *fb, int *lenp);

/* Write NUL terminated string data into the buffer and appends a
 * newline.  Returns number of bytes written on success.
 */
int fbuf_write_line (struct fbuf *fb, const char *data);

/* Read up to [len] bytes from buffer to file descriptor [fd] without
 * consuming data.  Set [len] to -1 to read all data.  Returns number
 * of bytes read or -1 on error. */
int fbuf_peek_to_fd (struct fbuf *fb, int fd, int len);

/* Read up to [len] bytes from buffer to file descriptor [fd] and mark
 * data as consumed.  Set [len] to -1 to read all data.  Returns
 * number of bytes read or -1 on error. */
int fbuf_read_to_fd (struct fbuf *fb, int fd, int len);

/* Write up to [len] bytes to buffer from file descriptor [fd].  Set
 * [len] to -1 to read an appropriate chunk size.  Returns number of
 * bytes written on success.
 */
int fbuf_write_from_fd (struct fbuf *fb, int fd, int len);

/* FUTURE: append, prepend, printf, add_fbuf, etc. */

#endif /* !_LIBSUBPROCESS_FBUF_H */
