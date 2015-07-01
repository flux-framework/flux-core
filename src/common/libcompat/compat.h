#ifndef _FLUX_COMPAT_H
#define _FLUX_COMPAT_H

/* handle */
#define flux_sendmsg                compat_sendmsg
#define flux_recvmsg                compat_recvmsg
#define flux_recvmsg_match          compat_recvmsg_match

/* reactor */
#define flux_msghandler_add         compat_msghandler_add
#define flux_msghandler_addvec      compat_msghandler_addvec
#define flux_msghandler_remove      compat_msghandler_remove
#define flux_fdhandler_add          compat_fdhandler_add
#define flux_fdhandler_remove       compat_fdhandler_remove
#define flux_zshandler_add          compat_zshandler_add
#define flux_zshandler_remove       compat_zshandler_remove
#define flux_tmouthandler_add       compat_tmouthandler_add
#define flux_tmouthandler_remove    compat_tmouthandler_remove

/* info */
#define flux_treeroot               compat_treeroot

#include "handle.h"
#include "reactor.h"
#include "info.h"

#endif /* !_FLUX_COMPAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
