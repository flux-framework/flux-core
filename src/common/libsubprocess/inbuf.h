#ifndef _FLUX_INBUF_H
#define _FLUX_INBUF_H

#include <flux/core.h>

enum {
    INBUF_LINE_BUFFERED = 1,
};

/* Set 'fd' to non-blocking and read from it through an internal buffer in
 * 'bufsize' chunks.  'cb' will be called when there is data to read,
 * according to 'flags' (see below).
 */
flux_watcher_t *flux_inbuf_watcher_create (flux_reactor_t *r, int fd,
                                           int bufsize, int flags,
                                           flux_watcher_f cb, void *arg);

/* No flags: read up to len bytes into buf.
 *   Returns the number of bytes read, or -1 on error with errno set.
 *   A return value of 0 indicates EOF.
 * flags & INBUF_LINE_BUFFERED: read one line of data into buf.
 *   'buf' will be NULL terminated and contain at most 'len' - 1 characters.
 *   Returns the number of characters read, or -1 on error with errno set.
 *   A return value >= len indicates that 'buf' was too short to contain
 *   the next line;  the portion that fit was returned, the rest discarded.
 *   A return value of 0 indicates EOF.
 */
int flux_inbuf_watcher_read (flux_watcher_t *w, void *buf, int len);

#endif /* !_FLUX_INBUF_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
