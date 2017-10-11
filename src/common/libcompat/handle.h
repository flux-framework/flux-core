#ifndef _FLUX_COMPAT_HANDLE_H
#define _FLUX_COMPAT_HANDLE_H

#include <stdbool.h>
#include <string.h>

#include "src/common/libflux/message.h"

#define flux_sendmsg                compat_sendmsg
#define flux_recvmsg                compat_recvmsg
#define flux_recvmsg_match          compat_recvmsg_match

int flux_sendmsg (flux_t *h, flux_msg_t **msg)
                  __attribute__ ((deprecated));

flux_msg_t *flux_recvmsg (flux_t *h, bool nonblock)
                          __attribute__ ((deprecated));

flux_msg_t *flux_recvmsg_match (flux_t *h, struct flux_match match,
                                bool nonblock)
                                __attribute__ ((deprecated));

#endif /* !_FLUX_COMPAT_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
