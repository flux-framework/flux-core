#ifndef _FLUX_CORE_HANDLE_IMPL_H
#define _FLUX_CORE_HANDLE_IMPL_H

#include <stdbool.h>
#include <czmq.h>

#include "handle.h"
#include "security.h"

/**
 ** Only handle implementation stuff below.
 ** Flux_t handle users should not use these interfaces.
 */

struct flux_handle_ops {
    int         (*request_sendmsg)(void *impl, zmsg_t **zmsg);
    zmsg_t *    (*request_recvmsg)(void *impl, bool nonblock);
    int         (*response_sendmsg)(void *impl, zmsg_t **zmsg);
    zmsg_t *    (*response_recvmsg)(void *impl, bool nonblock);
    int         (*response_putmsg)(void *impl, zmsg_t **zmsg);

    zmsg_t *    (*event_recvmsg)(void *impl, bool nonblock);
    int         (*event_subscribe)(void *impl, const char *topic);
    int         (*event_unsubscribe)(void *impl, const char *topic);

    zmsg_t *    (*snoop_recvmsg)(void *impl, bool nonblock);
    int         (*snoop_subscribe)(void *impl, const char *topic);
    int         (*snoop_unsubscribe)(void *impl, const char *topic);

    int         (*rank)(void *impl);

    zctx_t *    (*get_zctx)(void *impl);

    int         (*reactor_start)(void *impl);
    void        (*reactor_stop)(void *impl, int rc);
    int         (*reactor_fd_add)(void *impl, int fd, short events);
    void        (*reactor_fd_remove)(void *impl, int fd, short events);
    int         (*reactor_zs_add)(void *impl, void *zs, short events);
    void        (*reactor_zs_remove)(void *impl, void *zs, short events);
    int         (*reactor_tmout_add)(void *impl, unsigned long msec,
                                     bool oneshot);
    void        (*reactor_tmout_remove)(void *impl, int timer_id);

    void        (*impl_destroy)(void *impl);
};

flux_t flux_handle_create (void *impl, const struct flux_handle_ops *ops, int flags);
void flux_handle_destroy (flux_t *hp);
int flux_handle_event_msg (flux_t h, int typemask, zmsg_t **zmsg);
int flux_handle_event_fd (flux_t h, int fd, short revents);
int flux_handle_event_zs (flux_t h, void *zs, short revents);
int flux_handle_event_tmout (flux_t h, int timer_id);
zctx_t *flux_get_zctx (flux_t h);

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
