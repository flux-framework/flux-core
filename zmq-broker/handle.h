#ifndef HAVE_FLUX_HANDLE_H
#define HAVE_FLUX_HANDLE_H

struct flux_handle_ops {
    int         (*request_sendmsg)(void *impl, zmsg_t **zmsg);
    zmsg_t *    (*request_recvmsg)(void *impl, bool nonblock);
    int         (*response_sendmsg)(void *impl, zmsg_t **zmsg);
    zmsg_t *    (*response_recvmsg)(void *impl, bool nonblock);
    int         (*response_putmsg)(void *impl, zmsg_t **zmsg);

    int         (*event_sendmsg)(void *impl, zmsg_t **zmsg);
    zmsg_t *    (*event_recvmsg)(void *impl, bool nonblock);
    int         (*event_subscribe)(void *impl, const char *topic);
    int         (*event_unsubscribe)(void *impl, const char *topic);
   
    zmsg_t *    (*snoop_recvmsg)(void *impl, bool nonblock);
    int         (*snoop_subscribe)(void *impl, const char *topic);
    int         (*snoop_unsubscribe)(void *impl, const char *topic);

    int         (*rank)(void *impl);

    zctx_t *    (*get_zctx)(void *impl);

    /* On the reactor interface:
     * The handle implementation "owns" the reactor (zloop or whatever).
     * The generic handle.c code registers three callbacks, one for messages
     * (Flux message types on Flux plumbing), one for events on file
     * descriptors, and one for events on zeromq sockets.  These handlers
     * are registered with the reactor_*handler_set() calls and there is
     * exactly one for each type.  File descriptors and zmq sockets are
     * added/removed using the reactor_*_add/remove() calls.  The three
     * callbacks will demultiplex messages to user callbacks, but that need
     * not concern the handle implementation.
     */
    int         (*reactor_start)(void *impl);
    void        (*reactor_stop)(void *impl);
    int         (*reactor_msghandler_set)(void *impl,
                                          FluxMsgHandler cb, void *arg);
    int         (*reactor_fdhandler_set)(void *impl,
                                          FluxFdHandler cb, void *arg);
    int         (*reactor_fd_add)(void *impl, int fd, short events);
    void        (*reactor_fd_remove)(void *impl, int fd, short events);
    int         (*reactor_zshandler_set)(void *impl,
                                          FluxZsHandler cb, void *arg);
    int         (*reactor_zs_add)(void *impl, void *zs, short events);
    void        (*reactor_zs_remove)(void *impl, void *zs, short events);
    int         (*reactor_tmouthandler_set)(void *impl,
                                          FluxTmoutHandler cb, void *arg);
    int         (*reactor_timeout_set)(void *impl, unsigned long msec);
    int         (*reactor_timeout_clear)(void *impl);
    bool        (*reactor_timeout_isset)(void *impl);

    void        (*impl_destroy)(void *impl);
};

flux_t flux_handle_create (void *impl, const struct flux_handle_ops *ops,
                           int flags);

#endif /* !HAVE_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
