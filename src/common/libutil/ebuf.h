#ifndef FLUX_EBUF_H
#define FLUX_EBUF_H

typedef struct ebuf ebuf_t;

typedef void (*ebuf_cb) (ebuf_t *eb, void *arg);

/* Create buffer.
 */
ebuf_t *ebuf_create (int maxsize);

void ebuf_destroy (void *eb);

/* Returns the number of bytes current stored in ebuf */
int ebuf_bytes (ebuf_t *eb);

/* Call [cb] when the number of bytes stored is greater than [low]
 * bytes.
 *
 * The callback is typically called after an ebuf_write() or other
 * write action has occurred on the buffer.  Often, users set [low] to
 * 0, so that the callback is called anytime data has been added to
 * the buffer.
 *
 * At most one callback handler can be set per ebuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int ebuf_set_low_read_cb (ebuf_t *eb, ebuf_cb cb, int low, void *arg);

/* Call [cb] when a line has been stored.  This callback is typically
 * called after an ebuf_write() or other write action has occurred on
 * the buffer.
 *
 * At most one callback handler can be set per ebuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int ebuf_set_read_line_cb (ebuf_t *eb, ebuf_cb cb, void *arg);

/* Call [cb] when the number of bytes stored falls less than
 * [high] bytes.  Setting [cb] to NULL disables the callback.
 *
 * This callback is generally called after a ebuf_drop(), ebuf_read()
 * or other consumption action has occurred on the buffer.  Often,
 * users set [high] to [maxsize], so that the callback is called when
 * the buffer has space for writing.
 *
 * At most one callback handler can be set per ebuf.  If another
 * callback type has already been set, EEXIST is the errno returned.
 * Setting [cb] to NULL disables the callback.
 */
int ebuf_set_high_write_cb (ebuf_t *eb, ebuf_cb cb, int high, void *arg);

/* Drop up to [len] bytes of data in the buffer. Set [len] to -1
 * to drop all data.  Returns number of bytes dropped on success.
 */
int ebuf_drop (ebuf_t *eb, int len);

/* Read up to [len] bytes of data in the buffer without consuming it.
 * Pointer to buffer is returned to user and optionally length read
 * can be returned to user in [lenp].  User shall not free returned
 * pointer.  Set [len] to -1 to read all data.
 */
const void *ebuf_peek (ebuf_t *eb, int len, int *lenp);

/* Read up to [len] bytes of data in the buffer and mark data as
 * consumed.  Pointer to buffer is returned to user and optionally
 * length read can be returned to user in [lenp].  User shall not free
 * returned pointer.  Set [len] to -1 to read all data.
 */
const void *ebuf_read (ebuf_t *eb, int len, int *lenp);

/* Determine if a line is available for peeking/reading.  Returns -1
 * on error, 1 for lines available, 0 for no lines available */
int ebuf_line (ebuf_t *eb);

/* Drop a line in the buffer.  Returns number of bytes dropped on
 * success. */
int ebuf_drop_line (ebuf_t *eb);

/* Read a line in the buffer without consuming it.  Return buffer will
 * include newline.  Optionally return length of data returned in
 * [lenp].  Return NULL on error or no line present.
 */
const void *ebuf_peek_line (ebuf_t *eb, int *lenp);

/* Read a line in the buffer and mark data as consumed.  Return buffer
 * will include newline.  Optionally return length of data returned in
 * [lenp].  Return NULL on error or no line present.
 */
const void *ebuf_read_line (ebuf_t *eb, int *lenp);

/* Write [len] bytes of data into the buffer.  Returns number of bytes
 * written on success.
 */
int ebuf_write (ebuf_t *eb, const void *data, int len);

/* Write NUL terminated string data into the buffer and appends a
 * newline.  Returns number of bytes written on success.
 */
int ebuf_write_line (ebuf_t *eb, const char *data);

/* FUTURE: append, prepend, printf, add_ebuf, etc. */

#endif /* !_FLUX_EBUF_H */
