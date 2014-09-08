#ifndef _FLUX_HANDLE_H
#define _FLUX_HANDLE_H

/* Flags for handle creation and flux_flags_set()/flux_flags_unset.
 */
enum {
    FLUX_FLAGS_TRACE = 1,   /* print 0MQ messages sent over the flux_t */
                            /*   handle on stdout. */
};

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The FluxFreeFn, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 */
typedef void (*FluxFreeFn)(void *arg);
void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy);

/* Set/clear FLUX_FLAGS_* on a flux_t handle.
 * Flags can also be set when the handle is created, e.g. flux_api_openpath().
 */
void flux_flags_set (flux_t h, int flags);
void flux_flags_unset (flux_t h, int flags);

/* Accessor for zeromq context.
 * N.B. The zctx_t is thread-safe but zeromq sockets, and therefore
 * flux_t handle operations are not.
 */
zctx_t *flux_get_zctx (flux_t h);

/* Accessor for security context.  Same comments on thread safety apply.
 */
flux_sec_t flux_get_sec (flux_t h);


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
    flux_sec_t  (*get_sec)(void *impl);

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

#endif /* !_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
