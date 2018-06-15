#ifndef FLUX_BUFFER_H
#define FLUX_BUFFER_H

typedef struct flux_buffer flux_buffer_t;

/* Create buffer.
 */
flux_buffer_t *flux_buffer_create (int size);

void flux_buffer_destroy (void *fb);

/* Returns the buffer size, set when flux_buffer_create () was called */
int flux_buffer_size (flux_buffer_t *fb);

/* Returns the number of bytes current stored in flux_buffer */
int flux_buffer_bytes (flux_buffer_t *fb);

/* Returns the number of bytes of space available in flux_buffer */
int flux_buffer_space (flux_buffer_t *fb);

/* Manage "readonly" status
 * - flux_buffer_readonly() makes it so writes are no longer allowed
 *   to the buffer.  Reads are allowed until the buffer is empty.
 *   Changing a buffer to "readonly" can only be called once and
 *   cannot be disabled.  This is a convenience status can be used to
 *   indicate to users that the buffer is no longer usable.
 * - flux_buffer_is_readonly() returns > 0 if a buffer is readonly, 0
 *   if not, and -1 on error.
 */
int flux_buffer_readonly (flux_buffer_t *fb);
int flux_buffer_is_readonly (flux_buffer_t *fb);

/* Drop up to [len] bytes of data in the buffer. Set [len] to -1
 * to drop all data.  Returns number of bytes dropped on success.
 */
int flux_buffer_drop (flux_buffer_t *fb, int len);

/* Read up to [len] bytes of data in the buffer without consuming it.
 * Pointer to buffer is returned to user and optionally length read
 * can be returned to user in [lenp].  User shall not free returned
 * pointer.  If no data is available, returns pointer and length of 0.
 * Set [len] to -1 to read all data.
 */
const void *flux_buffer_peek (flux_buffer_t *fb, int len, int *lenp);

/* Read up to [len] bytes of data in the buffer and mark data as
 * consumed.  Pointer to buffer is returned to user and optionally
 * length read can be returned to user in [lenp].  User shall not free
 * returned pointer.  If no data is available, returns pointer and
 * length of 0.  Set [len] to -1 to read all data.
 */
const void *flux_buffer_read (flux_buffer_t *fb, int len, int *lenp);

/* Write [len] bytes of data into the buffer.  Returns number of bytes
 * written on success.
 */
int flux_buffer_write (flux_buffer_t *fb, const void *data, int len);

/* Determines lines available for peeking/reading.  Returns -1
 * on error, >= 0 for lines available */
int flux_buffer_lines (flux_buffer_t *fb);

/* Drop a line in the buffer.  Returns number of bytes dropped on
 * success. */
int flux_buffer_drop_line (flux_buffer_t *fb);

/* Read a line in the buffer without consuming it.  Return buffer will
 * include newline.  Optionally return length of data returned in
 * [lenp].  If no line is available, returns pointer and length of
 * 0. Return NULL on error.
 */
const void *flux_buffer_peek_line (flux_buffer_t *fb, int *lenp);

/* Read a line in the buffer and mark data as consumed.  Return buffer
 * will include newline.  Optionally return length of data returned in
 * [lenp].  If no line is available, returns pointer and length of 0.
 * Return NULL on error.
 */
const void *flux_buffer_read_line (flux_buffer_t *fb, int *lenp);

/* Write NUL terminated string data into the buffer and appends a
 * newline.  Returns number of bytes written on success.
 */
int flux_buffer_write_line (flux_buffer_t *fb, const char *data);

/* Read up to [len] bytes from buffer to file descriptor [fd] without
 * consuming data.  Set [len] to -1 to read all data.  Returns number
 * of bytes read or -1 on error. */
int flux_buffer_peek_to_fd (flux_buffer_t *fb, int fd, int len);

/* Read up to [len] bytes from buffer to file descriptor [fd] and mark
 * data as consumed.  Set [len] to -1 to read all data.  Returns
 * number of bytes read or -1 on error. */
int flux_buffer_read_to_fd (flux_buffer_t *fb, int fd, int len);

/* Write up to [len] bytes to buffer from file descriptor [fd].  Set
 * [len] to -1 to read an appropriate chunk size.  Returns number of
 * bytes written on success.
 */
int flux_buffer_write_from_fd (flux_buffer_t *fb, int fd, int len);

/* FUTURE: append, prepend, printf, add_flux_buffer, etc. */

#endif /* !_FLUX_BUFFER_H */
