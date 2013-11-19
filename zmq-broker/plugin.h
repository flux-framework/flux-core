#ifndef PLUGIN_H
#define PLUGIN_H

#include "flux.h"

#define UPREQ_URI           "inproc://upreq"
#define DNREQ_URI           "inproc://dnreq"
#define DNEV_OUT_URI        "inproc://evout"
#define DNEV_IN_URI         "inproc://evin"
#define SNOOP_URI           "inproc://snoop"

/* A plugin defines 'const struct plugin_ops ops = {...}' containing
 * its implementations of one or more of the plugin operations.
 * It is compiled into a .so which the plugin loader dlopens.
 */

typedef enum {
    ZMSG_REQUEST, ZMSG_RESPONSE, ZMSG_EVENT, ZMSG_SNOOP }
zmsg_type_t;

struct plugin_ops {
    void (*timeout)(flux_t h);
    void (*recv)(flux_t h, zmsg_t **zmsg, zmsg_type_t type);
    int (*init)(flux_t h, zhash_t *args);
    void (*fini)(flux_t h);
};

typedef struct plugin_ctx_struct *plugin_ctx_t;

plugin_ctx_t plugin_load (zctx_t *zctx, int rank, int size, bool treeroot,
                          char *name, char *id, zhash_t *args);
void plugin_unload (plugin_ctx_t p);

#endif /* !PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
