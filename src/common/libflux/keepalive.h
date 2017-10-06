#ifndef _FLUX_CORE_KEEPALIVE_H
#define _FLUX_CORE_KEEPALIVE_H

#ifdef __cplusplus
extern "C" {
#endif

flux_msg_t *flux_keepalive_encode (int errnum, int status);

int flux_keepalive_decode (const flux_msg_t *msg, int *errnum, int *status);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KEEPALIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
