#ifndef _FLUX_CORE_KEEPALIVE_H
#define _FLUX_CORE_KEEPALIVE_H

flux_msg_t *flux_keepalive_encode (int errnum, int status);

int flux_keepalive_decode (const flux_msg_t *msg, int *errnum, int *status);

#endif /* !_FLUX_CORE_KEEPALIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
