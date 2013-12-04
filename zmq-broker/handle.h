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

    int         (*reactor_start)(void *impl);
    void        (*reactor_stop)(void *impl, int rc);
    int         (*reactor_fd_add)(void *impl, int fd, short events);
    void        (*reactor_fd_remove)(void *impl, int fd, short events);
    int         (*reactor_zs_add)(void *impl, void *zs, short events);
    void        (*reactor_zs_remove)(void *impl, void *zs, short events);
    int         (*reactor_timeout_set)(void *impl, unsigned long msec);

    void        (*impl_destroy)(void *impl);
};

/* These functions should only be called from handle implementations.
 */
flux_t handle_create (void *impl, const struct flux_handle_ops *ops, int flags);
int handle_event_msg (flux_t h, int typemask, zmsg_t **zmsg);
int handle_event_fd (flux_t h, int fd, short revents);
int handle_event_zs (flux_t h, void *zs, short revents);
int handle_event_tmout (flux_t h);

#endif /* !HAVE_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
