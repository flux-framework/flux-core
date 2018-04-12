#ifndef _FLUX_CORE_KZ_H
#define _FLUX_CORE_KZ_H

#include <flux/core.h>

typedef struct kz_struct kz_t;

typedef void (*kz_ready_f) (kz_t *kz, void *arg);

enum kz_flags {
    KZ_FLAGS_READ           = 0x0001, /* choose read or write, not both */
    KZ_FLAGS_WRITE          = 0x0002,
    KZ_FLAGS_MODEMASK       = 0x0003,

    KZ_FLAGS_NONBLOCK       = 0x0010, /* currently only applies to reads */
    KZ_FLAGS_NOFOLLOW       = 0x0020, /* currently only applies to reads */

    KZ_FLAGS_RAW            = 0x0200, /* use only *_json I/O methods */
    KZ_FLAGS_NOCOMMIT_OPEN  = 0x0400, /* skip commit at open (FLAGS_WRITE) */
    KZ_FLAGS_NOCOMMIT_PUT   = 0x0800, /* skip commit at put */
    KZ_FLAGS_NOCOMMIT_CLOSE = 0x1000, /* skip commit at close */

    KZ_FLAGS_DELAYCOMMIT    = (KZ_FLAGS_NOCOMMIT_OPEN | KZ_FLAGS_NOCOMMIT_PUT),
};

/* Prepare to read or write a KVS stream.
 * If open for writing, any existing content is overwritten.
 * If open for reading, KVS directory for stream need not exist
 */
kz_t *kz_open (flux_t *h, const char *name, int flags);

/* Write one block of data to a KVS stream.  Unless kz was opened with
 * KZ_FLAGS_DELAYCOMMIT, data will committed to the KVS.
 * Returns len (no short writes) or -1 on error with errno set.
 */
int kz_put (kz_t *kz, char *data, int len);

/* Read one block of data from a KVS stream.  Returns the number of bytes
 * read, 0 on EOF, or -1 on errro with errno set.  If no data is available,
 * kz_get() will return EAGAIN if kz was opened with KZ_FLAGS_NONBLOCK;
 * otherwise it will block until data is available.
 */
int kz_get (kz_t *kz, char **datap);

/* Commit any data written to the stream which has not already
 * been committed.  Calling this on a kz opened with KZ_FLAGS_READ is a no-op.
 */
int kz_flush (kz_t *kz);

/* Destroy the kz handle.  If kz was opened with KZ_FLAGS_WRITE, write
 * an EOF and commit any data written to the strewam  which has not already
 * been committed.
 */
int kz_close (kz_t *kz);

/* Register a callback that will be called when data is available to
 * be read from kz.  Call kz_open with (KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK).
 * Your function may call kz_get() once without blocking.
 */
int kz_set_ready_cb (kz_t *kz, kz_ready_f ready_cb, void *arg);

/* "Raw" get/put methods that use JSON objects encoded/decoded with
 * zio_json_encode/zio_json_decode.  The KVS stream must have been opened
 * with the KZ_FLAGS_RAW option, and these methods cannot be mixed with
 * the character-oriented methods.  EOF is handled in-band with these methods
 * (get/put JSON objects with the EOF flag set).  Simply closing the KVS stream
 * does not result in an EOF.
 */
/* Put a JSON object.  Returns 0 on success, -1 on failure, with errno set.
 * Caller retains ownership of 'o'.
 */
int kz_put_json (kz_t *kz, const char *json_str);

/* Get a JSON string.  Returns the object or NULL on failure with errno set.
 * Caller must free the returned string.
 */
char *kz_get_json (kz_t *kz);

#endif /* !FLUX_CORE_KZ_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
