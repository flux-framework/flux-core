#ifndef _FLUX_CORE_ZIO_H
#define _FLUX_CORE_ZIO_H 1

#include <flux/core.h>

typedef struct zio_ctx zio_t;

typedef int  (*zio_send_f)   (zio_t *z, const char *s, int len, void *arg);
typedef int  (*zio_close_f)  (zio_t *z, void *arg);
typedef void (*zio_log_f)    (const char *buf);

/*
 *  Create a zio "reader" object, which reads from fd [src] into a
 *   buffer (depending on buffer setting), and then calls a designated
 *   callback.  Use zio_set_send_cb() to set callback.
 */
zio_t *zio_reader_create (const char *name, int srcfd, void *arg);

/*
 *  Create a zio reader which reads from an internal pipe and sends
 *   json-encoded output to a zmq socket. Use zio_dst_fd() to get the
 *   file descriptor for the write side of the pipe.
 */
zio_t *zio_pipe_reader_create (const char *name, void *arg);

/*
 *  Create a zio "writer" object, that buffers data via zio_write_* interface
 *   and sends to the fd [dstfd].
 */
zio_t *zio_writer_create (const char *name, int dstfd, void *arg);

/*
 *   Create a zio writer which writes to an internal pipe (e.g. for stdin).
 *   Use zio_src_fd() to get the read side of the pipe.
 */
zio_t *zio_pipe_writer_create (const char *name, void *arg);

/*
 *  Destroy a zio object.
 */
void zio_destroy (zio_t *zio);

/*
 *  Return the name encoded with zio object.
 */
const char * zio_name (zio_t *zio);

/*
 *  Return reader fd of a zio object. (read side of pipe)
 */
int zio_src_fd (zio_t *zio);

/*
 *  Return write fd of a zio object (write side of pipe).
 */
int zio_dst_fd (zio_t *zio);

/*
 *  Close the src/dst file descriptors, if open
 */
int zio_close_src_fd (zio_t *zio);
int zio_close_dst_fd (zio_t *zio);

/*
 *  Check to see if zio object has been "closed". A zio object is closed
 *   after EOF has been read and sent (for reader) or received by writer
 *   and close(2) called on dstfd.
 */
int zio_closed (zio_t *zio);

/*
 *  Non-blocking read from zio object. Will read from zio object's src fd
 *   and buffer I/O according to buffering policy of object. Callbacks
 *   will be called synchronously if required by buffering policy.
 */
int zio_read (zio_t *zio);

/*  Non-blocking write directly to zio object. Data will be buffered by
 *   zio object and written to destination fd when ready, if zio object
 *   is registered in an event loop.
 */
int zio_write (zio_t *zio, void *data, size_t len);

/*
 *  Set EOF on zio object [zio].
 */
int zio_write_eof (zio_t *zio);

/*
 *  Write data from json object [o] to zio object [z], data is buffered
 *   if necessary. Only data destined for specific object [z] is read.
 */
int zio_write_json (zio_t *z, const char *json_str);

/*
 *   Attach zio object [x] to flux reactor.
 *    zio object will be automatcially detached after EOF is
 *    received and sent.  ZIO readers will use reactor callback
 *    to schedule reads when data is ready. ZIO writers will
 *    use reactor to schedule writes to dstfd when it is ready
 *    for writing.
 */
int zio_reactor_attach (zio_t *z, flux_reactor_t *reactor);

/*
 *   Same as above but use reactor associated with flux_t *handle.
 */
int zio_flux_attach (zio_t *z, flux_t *h);

/*
 *  ZIO buffering options:
 */
int zio_set_unbuffered (zio_t *zio);
int zio_set_buffered (zio_t *zio, size_t bufsize);

/*
 *  Line buffer zio object, and send data (or invoke callback) for
 *   every N [lines]. If lines == -1, then send as many lines as
 *   are available (e.g. multiple lines may be received per callback)
 */
int zio_set_line_buffered (zio_t *zio, int lines);

/*
 *  Enable zio debug output for this zio object. Optional
 *   prefix is prepended to messages instead of zio object name,
 *   and the optional zio_log_f pointer can override the default
 *   output to stderr.
 */
int zio_set_debug (zio_t *zio, const char *prefix, zio_log_f logf);

/*
 *  Disable any debug for zio object [zio].
 */
int zio_set_quiet (zio_t *zio);

/*
 *  Set zio callback to return raw string data instead of json
 *   object.
 */
int zio_set_raw_output (zio_t *zio);

/*
 *  Set the send() function for ZIO readers.
 */
int zio_set_send_cb (zio_t *zio, zio_send_f sendf);

/*
 *  Set a callback function that is called just after a zio object
 *   is automatically "closed" (See description for zio_closed() for
 *   more info on how ZIO objects are closed)
 */
int zio_set_close_cb (zio_t *zio, zio_close_f closef);

/*
 *  Flush buffered data.  Calls send callback for zio READER objects
 *   or writes to dstfd on zio WRITER objects.
 */
int zio_flush (zio_t *zio);

/*
 *  Read data/eof from object.
 *   Returns size of data or -1 on error.
 */
int zio_json_decode (const char *json_str, void **pp, bool *eofp);

/*  Create object.  Returns NULL on failure.
 */
char *zio_json_encode (void *p, int len, bool eof);

/*  Test if object has eof=true.
 */
bool zio_json_eof (const char *json_str);

#endif /* !_FLUX_CORE_ZIO_H */
