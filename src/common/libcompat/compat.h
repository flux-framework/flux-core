#ifndef _FLUX_COMPAT_H
#define _FLUX_COMPAT_H

#define flux_sendmsg        compat_sendmsg
#define flux_recvmsg        compat_recvmsg
#define flux_recvmsg_match  compat_recvmsg_match

#include "handle.h"

#endif /* !_FLUX_COMPAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
