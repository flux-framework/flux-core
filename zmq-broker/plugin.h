#ifndef PLUGIN_H
#define PLUGIN_H

#include "kvs.h"

typedef struct plugin_struct *plugin_t;

typedef struct {
    int upreq_send_count;
    int upreq_recv_count;
    int dnreq_send_count;
    int dnreq_recv_count;
    int event_send_count;
    int event_recv_count;
} plugin_stats_t;

typedef struct ptimeout_struct *ptimeout_t;

typedef struct {
    conf_t *conf;
    void *zs_upreq; /* for making requests */
    void *zs_dnreq; /* for handling requests (reverse message flow) */
    void *zs_evin;
    void *zs_evout;
    void *zs_snoop;
    char *id;
    ptimeout_t timeout;
    pthread_t t;
    plugin_t plugin;
    server_t *srv;
    plugin_stats_t stats;
    zloop_t *zloop;
    kvsctx_t kvs_ctx;
    zlist_t *deferred_responses;
    void *ctx;
} plugin_ctx_t;

typedef enum {
    ZMSG_REQUEST, ZMSG_RESPONSE, ZMSG_EVENT, ZMSG_SNOOP }
zmsg_type_t;

struct plugin_struct {
    const char *name;
    void (*timeoutFn)(plugin_ctx_t *p);
    void (*recvFn)(plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type);
    void (*initFn)(plugin_ctx_t *p);
    void (*finiFn)(plugin_ctx_t *p);
};

/* call from cmbd */
void plugin_init (conf_t *conf, server_t *srv);
void plugin_fini (conf_t *conf, server_t *srv);

/* call from plugin */
void plugin_send_request_raw (plugin_ctx_t *p, zmsg_t **zmsg);
void plugin_send_request (plugin_ctx_t *p, json_object *o, const char *fmt, ...);
json_object *plugin_request (plugin_ctx_t *p, json_object *o,
                             const char *fmt, ...);

void plugin_send_response_raw (plugin_ctx_t *p, zmsg_t **zmsg);
void plugin_send_response (plugin_ctx_t *p, zmsg_t **req, json_object *o);
void plugin_send_response_errnum (plugin_ctx_t *p, zmsg_t **req, int errnum);

void plugin_send_event_raw (plugin_ctx_t *p, zmsg_t **zmsg);
void plugin_send_event (plugin_ctx_t *p, const char *fmt, ...);
void plugin_send_event_json (plugin_ctx_t *p, json_object *o,
                             const char *fmt, ...);

void plugin_ping_respond (plugin_ctx_t *p, zmsg_t **zmsg);
void plugin_stats_respond (plugin_ctx_t *p, zmsg_t **zmsg);

void plugin_timeout_set (plugin_ctx_t *p, unsigned long val);
void plugin_timeout_clear (plugin_ctx_t *p);
bool plugin_timeout_isset (plugin_ctx_t *p);

void plugin_log (plugin_ctx_t *p, int lev, const char *fmt, ...);

bool plugin_treeroot (plugin_ctx_t *p);

#endif /* !PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
