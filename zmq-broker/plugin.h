#ifndef PLUGIN_H
#define PLUGIN_H

#include "flux.h"

/* Plugins will be connected to these well-known shared memory zmq sockets.
 */
#define REQUEST_URI         "inproc://request"
#define EVENT_URI           "inproc://event"

/* Plugin must define a mod_main().
 */
typedef int (mod_main_f)(flux_t h, zhash_t *args);
extern mod_main_f mod_main;

/* Plugin must define its service name.
 */
#define MOD_NAME(x) const char *mod_name = x

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
