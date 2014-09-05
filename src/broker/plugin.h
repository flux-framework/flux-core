#ifndef PLUGIN_H
#define PLUGIN_H

#include "flux.h"

/* Plugins will be connected to these well-known shared memory zmq sockets.
 */
#define REQUEST_URI         "inproc://request"
#define EVENT_URI           "inproc://event"

char *plugin_getstring (const char *path, const char *name);

/* Create, start, stop, destroy a plugin.
 * Termination:  plugin_stop (), read EOF on sock, plugin_destroy ()
 */
typedef struct plugin_ctx_struct *plugin_ctx_t;
plugin_ctx_t plugin_create (flux_t h, const char *path, zhash_t *args);
void plugin_start (plugin_ctx_t p);
void plugin_stop (plugin_ctx_t p);
void plugin_destroy (plugin_ctx_t p);


/* Accessors.
 */
const char *plugin_name (plugin_ctx_t p);
const char *plugin_uuid (plugin_ctx_t p);
const char *plugin_digest (plugin_ctx_t p);
int plugin_size (plugin_ctx_t p);
void *plugin_sock (plugin_ctx_t p);

#endif /* !PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
