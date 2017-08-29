#ifndef _FLUX_CORE_MSG_HANDLER_H
#define _FLUX_CORE_MSG_HANDLER_H

#include "message.h"
#include "handle.h"

typedef struct flux_msg_handler flux_msg_handler_t;

typedef void (*flux_msg_handler_f)(flux_t *h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg);

flux_msg_handler_t *flux_msg_handler_create (flux_t *h,
                                             const struct flux_match match,
                                             flux_msg_handler_f cb, void *arg);

void flux_msg_handler_destroy (flux_msg_handler_t *w);

void flux_msg_handler_start (flux_msg_handler_t *w);
void flux_msg_handler_stop (flux_msg_handler_t *w);

/* By default, only messages from FLUX_ROLE_OWNER are delivered to handler.
 * Use _allow_rolemask() add roles, _deny_rolemask() to remove them.
 * (N.B. FLUX_ROLE_OWNER cannot be denied)
 */
void flux_msg_handler_allow_rolemask (flux_msg_handler_t *w, uint32_t rolemask);
void flux_msg_handler_deny_rolemask (flux_msg_handler_t *w, uint32_t rolemask);

struct flux_msg_handler_spec {
    int typemask;
    char *topic_glob;
    flux_msg_handler_f cb;
    uint32_t rolemask;
    flux_msg_handler_t *w;
};
#define FLUX_MSGHANDLER_TABLE_END { 0, NULL, NULL, 0, NULL }

int flux_msg_handler_addvec (flux_t *h, struct flux_msg_handler_spec tab[],
                             void *arg);
void flux_msg_handler_delvec (struct flux_msg_handler_spec tab[]);

/* Requeue any unmatched messages, if handle was cloned.
 */
int flux_dispatch_requeue (flux_t *h);

#endif /* !_FLUX_CORE_MSG_HANDLER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
