#ifndef PLUGIN_H
#define PLUGIN_H

#include "flux.h"

typedef enum {
    ZMSG_REQUEST, ZMSG_RESPONSE, ZMSG_EVENT, ZMSG_SNOOP }
zmsg_type_t;

struct plugin_ops {
    const char *name;
    void (*timeout)(flux_t h);
    void (*recv)(flux_t h, zmsg_t **zmsg, zmsg_type_t type);
    void (*init)(flux_t h);
    void (*fini)(flux_t h);
};

/* call from cmbd to initialize/finalize the plugin loader
 */
void plugin_init (conf_t *conf, server_t *srv);
void plugin_fini (conf_t *conf, server_t *srv);

#endif /* !PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
