#ifndef _FLUX_COMPAT_HANDLE_H
#define _FLUX_COMPAT_HANDLE_H

#include <stdbool.h>
#include <string.h>

#include "src/common/libflux/message.h"

int flux_sendmsg (flux_t h, flux_msg_t **msg);
flux_msg_t *flux_recvmsg (flux_t h, bool nonblock);
flux_msg_t *flux_recvmsg_match (flux_t h, struct flux_match match,
                                bool nonblock);

#endif /* !_FLUX_COMPAT_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
