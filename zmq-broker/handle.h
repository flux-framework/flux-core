#ifndef HAVE_FLUX_HANDLE_H
#define HAVE_FLUX_HANDLE_H

typedef int FluxSendMsg(void *impl, zmsg_t **zmsg);
typedef zmsg_t *FluxRecvMsg(void *impl, bool nonblock);
typedef int FluxPutMsg(void *impl, zmsg_t **zmsg);
typedef int FluxSub(void *impl, const char *topic);
typedef int FluxTimeoutSet(void *impl, unsigned long msec);
typedef int FluxTimeoutClear (void *impl);
typedef zloop_t *FluxGetZloop (void *impl);
typedef zctx_t *FluxGetZctx (void *impl);

typedef int FluxGetInt(void *impl);
typedef bool FluxGetBool(void *impl);

struct flux_handle_struct {
    FluxSendMsg         *request_sendmsg;
    FluxRecvMsg         *request_recvmsg;
    FluxSendMsg         *response_sendmsg;
    FluxRecvMsg         *response_recvmsg;
    FluxPutMsg          *response_putmsg;

    FluxSendMsg         *event_sendmsg;
    FluxRecvMsg         *event_recvmsg;
    FluxSub             *event_subscribe;
    FluxSub             *event_unsubscribe;

    FluxRecvMsg         *snoop_recvmsg;
    FluxSub             *snoop_subscribe;
    FluxSub             *snoop_unsubscribe;

    FluxTimeoutSet      *timeout_set;
    FluxTimeoutClear    *timeout_clear;
    FluxGetBool         *timeout_isset;

    FluxGetZloop        *get_zloop;
    FluxGetZctx         *get_zctx;

    FluxGetInt          *rank;
    FluxGetInt          *size;
    FluxGetBool         *treeroot;

    int                 flags;

    zhash_t             *aux;
};

flux_t flux_handle_create (void *impl, FluxFreeFn *impl_destroy, int flags);

#endif /* !HAVE_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
