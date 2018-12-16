#ifndef _FLUX_KVS_WATCH_PRIVATE_H
#define _FLUX_KVS_WATCH_PRIVATE_H

#define CLASSIC_WATCH_FLAGS \
    (FLUX_KVS_WATCH \
   | FLUX_KVS_WAITCREATE \
   | FLUX_KVS_WATCH_FULL \
   | FLUX_KVS_WATCH_UNIQ)

#define CLASSIC_DIR_WATCH_FLAGS \
    (CLASSIC_WATCH_FLAGS \
   | FLUX_KVS_READDIR)

/* Synchronously cancel the stream of lookup responses.
 * Per RFC 6, once any error is returned, stream has ended.
 * This function destroys any value currently in future container.
 * If stream terminates with ENODATA, return 0, otherwise -1 with errno set.
 */
int kvs_cancel_streaming_lookup (flux_future_t *f);

#endif  /* !_FLUX_KVS_WATCH_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
