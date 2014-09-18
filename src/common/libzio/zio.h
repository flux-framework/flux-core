#ifndef _FLUX_CORE_ZIO_H
#define _FLUX_CORE_ZIO_H 1

#include <json.h>
#include <czmq.h>
#include <flux/core.h>

typedef struct zio_ctx * zio_t;

typedef int  (*zio_send_f)   (zio_t z, json_object *o, void *arg);
typedef int  (*zio_close_f)  (zio_t z, void *arg);
typedef void (*zio_log_f)    (const char *buf);

/*
 *  Create a zio "reader" object, which reads from fd [src] into a buffer
 *   (depending on buffer setting), and sends json-encoded output to
 *    [dst] zeromq socket.
 */
zio_t zio_reader_create (const char *name, int src, void *dst, void *arg);

/*
 *  Create a zio reader which reads from an internal pipe and sends
 *   json-encoded output to a zmq socket. Use zio_dst_fd() to get the
 *   file descriptor for the write side of the pipe.
 */
zio_t zio_pipe_reader_create (const char *name, void *dst, void *arg);

/*
 *  Create a zio "writer" object, that buffers data via zio_write_* interface
 *   and sends to the fd [dstfd].
 */
zio_t zio_writer_create (const char *name, int dstfd, void *arg);

/*
 *   Create a zio writer which writes to an internal pipe (e.g. for stdin).
 *   Use zio_src_fd() to get the read side of the pipe.
 */
zio_t zio_pipe_writer_create (const char *name, void *arg);

/*
 *  Destroy a zio object.
 */
void zio_destroy (zio_t zio);

/*
 *  Return the name encoded with zio object.
 */
const char * zio_name (zio_t zio);

/*
 *  Return reader fd of a zio object. (read side of pipe)
 */
int zio_src_fd (zio_t zio);

/*
 *  Return write fd of a zio object (write side of pipe).
 */
int zio_dst_fd (zio_t zio);

/*
 *  Check to see if zio object has been "closed". A zio object is closed
 *   after EOF has been read and sent (for reader) or received by writer
 *   and close(2) called on dstfd.
 */
int zio_closed (zio_t zio);

/*
 *  Write data from json object [o] to zio object [z], data is buffered
 *   if necessary. Only data destined for specific object [z] is read,
 *   and the data is consumed after reading.
 */
int zio_write_json (zio_t z, json_object *o);

/*
 *   Attach zio object [x] to zloop poll loop [zloop].
 *    zio object will be automatcially detached after EOF is
 *    received and sent.  ZIO readers will use zloop callback
 *    to schedule reads when data is ready. ZIO writers will
 *    use zloop to schedule writes to dstfd when it is ready
 *    for writing.
 */
int zio_zloop_attach (zio_t z, zloop_t *zloop);

/*
 *   Attach zio object [x] to flux reactor in handle [flux].
 *    zio object will be automatcially detached after EOF is
 *    received and sent.  ZIO readers will use reactor callback
 *    to schedule reads when data is ready. ZIO writers will
 *    use reactor to schedule writes to dstfd when it is ready
 *    for writing.
 */
int zio_flux_attach (zio_t z, flux_t flux);

/*
 *  ZIO buffering options:
 */
int zio_set_unbuffered (zio_t zio);
int zio_set_buffered (zio_t zio, size_t bufsize);
int zio_set_line_buffered (zio_t zio);

/*
 *  Enable zio debug output for this zio object. Optional
 *   prefix is prepended to messages instead of zio object name,
 *   and the optional zio_log_f pointer can override the default
 *   output to stderr.
 */
int zio_set_debug (zio_t zio, const char *prefix, zio_log_f logf);

/*
 *  Disable any debug for zio object [zio].
 */
int zio_set_quiet (zio_t zio);

/*
 *  Override the default send() function for ZIO readers. (Default
 *   send function uses zmsg_send() to dst sock)
 */
int zio_set_send_cb (zio_t zio, zio_send_f sendf);

/*
 *  Set a callback function that is called just after a zio object
 *   is automatically "closed" (See description for zio_closed() for
 *   more info on how ZIO objects are closed)
 */
int zio_set_close_cb (zio_t zio, zio_close_f closef);


int zio_flush (zio_t zio);

/*
 *  Read data/eof from object.
 *   Returns size of data or -1 on error.
 */
int zio_json_decode (json_object *o, void **pp, bool *eofp);

/*  Create object.  Returns NULL on failure.
 */
json_object *zio_json_encode (void *p, int len, bool eof);

/*  Test if object has eof=true.
 */
bool zio_json_eof (json_object *o);

#endif /* !_FLUX_CORE_ZIO_H */
