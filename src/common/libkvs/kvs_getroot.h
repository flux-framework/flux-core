#ifndef _FLUX_CORE_KVS_GETROOT_H
#define _FLUX_CORE_KVS_GETROOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Request the current KVS root hash for namespace 'ns'.
 * If flags = FLUX_KVS_WATCH, a response is sent each time the root
 * hash changes.  In that case, the user must call flux_future_reset()
 * after consuming the response to re-arm the future for the next response.
 */
flux_future_t *flux_kvs_getroot (flux_t *h, const char *ns, int flags);

/* Decode KVS root hash response.
 *
 * treeobj - get the hash as an RFC 11 "dirref" object.
 * blobref - get the raw hash as a n RFC 10 "blobref".
 * sequence - get the commit sequence number
 * owner - get the userid of the namespace owner
 */
int flux_kvs_getroot_get_treeobj (flux_future_t *f, const char **treeobj);
int flux_kvs_getroot_get_blobref (flux_future_t *f, const char **blobref);
int flux_kvs_getroot_get_sequence (flux_future_t *f, int *seq);
int flux_kvs_getroot_get_owner (flux_future_t *f, uint32_t *owner);

/* Cancel a FLUX_KVS_WATCH "stream".
 * Once the cancel request is processed, an ENODATA error response is sent,
 * thus the user should continue to reset and consume responses until an
 * error occurs, after which it is safe to destroy the future.
 */
int flux_kvs_getroot_cancel (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_GETROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
