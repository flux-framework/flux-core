#ifndef HAVE_FLUX_HANDLE_H
#define HAVE_FLUX_HANDLE_H

typedef int FluxRequestSendMsg(void *impl, zmsg_t **zmsg);
typedef int FluxResponseRecvMsg(void *impl, zmsg_t **zmsg, bool nonblock);
typedef int FluxResponsePutMsg(void *impl, zmsg_t **zmsg);

typedef int FluxEventSendMsg(void *impl, zmsg_t **zmsg);
typedef int FluxEventRecvMsg(void *impl, zmsg_t **zmsg, bool nonblock);
typedef int FluxEventSub(void *impl, const char *topic);
typedef int FluxEventUnsub(void *impl, const char *topic);

typedef int FluxSnoopRecvMsg(void *impl, zmsg_t **zmsg, bool nonblock);
typedef int FluxSnoopSub(void *impl, const char *topic);
typedef int FluxSnoopUnsub(void *impl, const char *topic);

typedef int FluxRank(void *impl);
typedef int FluxSize(void *impl);

struct flux_handle_struct {
    FluxRequestSendMsg      *request_sendmsg;
    FluxResponseRecvMsg     *response_recvmsg;
    FluxResponsePutMsg      *response_putmsg;

    FluxEventSendMsg        *event_sendmsg;
    FluxEventRecvMsg        *event_recvmsg;
    FluxEventSub            *event_subscribe;
    FluxEventUnsub          *event_unsubscribe;

    FluxSnoopRecvMsg        *snoop_recvmsg;
    FluxSnoopSub            *snoop_subscribe;
    FluxSnoopUnsub          *snoop_unsubscribe;
    FluxRank                *rank;
    FluxSize                *size;

    int                     flags;

    zhash_t                 *aux;
};

flux_t flux_handle_create (void *impl, FluxFreeFn *impl_destroy, int flags);

#endif /* !HAVE_FLUX_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
