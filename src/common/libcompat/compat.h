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
#define flux_tmouthandler_add       compat_tmouthandler_add
#define flux_tmouthandler_remove    compat_tmouthandler_remove
#define flux_reactor_start          compat_reactor_start

/* info */
#define flux_rank                   compat_rank
#define flux_size                   compat_size

/* request */
#define flux_json_request           compat_request
#define flux_json_respond           compat_respond

/* rpc */
#define flux_json_rpc               compat_rpc
#define flux_json_multrpc           compat_multrpc

/* module */
#define flux_modfind                compat_modfind
#define flux_modname                compat_modname

#include "handle.h"
#include "reactor.h"
#include "info.h"
#include "request.h"
#include "rpc.h"
#include "module.h"

#endif /* !_FLUX_COMPAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
