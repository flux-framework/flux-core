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

    int         (*timeout_set)(void *impl, unsigned long msec);
    int         (*timeout_clear)(void *impl);
    bool        (*timeout_isset)(void *impl);

    zloop_t *   (*get_zloop)(void *impl);
    zctx_t *    (*get_zctx)(void *impl);

    int         (*rank)(void *impl);
    int         (*size)(void *impl);
    bool        (*treeroot)(void *impl);

    void        (*impl_destroy)(void *impl);
};

flux_t flux_handle_create (void *impl, const struct flux_handle_ops *ops,
                           int flags);

#endif /* !HAVE_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
