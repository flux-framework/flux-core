#ifndef PLUGIN_H
#define PLUGIN_H

#include "flux.h"

typedef enum {
    ZMSG_REQUEST, ZMSG_RESPONSE, ZMSG_EVENT, ZMSG_SNOOP }
zmsg_type_t;

typedef struct plugin_struct *plugin_t;
struct plugin_struct {
    const char *name;
    void (*timeoutFn)(flux_t h);
    void (*recvFn)(flux_t h, zmsg_t **zmsg, zmsg_type_t type);
    void (*initFn)(flux_t h);
    void (*finiFn)(flux_t h);
};

/* call from cmbd to initialize/finalize the plugin loader
 */
void plugin_init (conf_t *conf, server_t *srv);
void plugin_fini (conf_t *conf, server_t *srv);

#endif /* !PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
