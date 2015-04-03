#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

/* Plugins will be connected to these well-known shared memory zmq sockets.
 */
#define REQUEST_URI         "inproc://request"
#define EVENT_URI           "inproc://event"

/* Create, start, stop, destroy a module.
 * Termination:  mod_stop (), read EOF on sock, mod_destroy ()
 */
typedef struct mod_ctx_struct *mod_ctx_t;
mod_ctx_t mod_create (flux_t h, const char *path, zhash_t *args);
void mod_start (mod_ctx_t p);
void mod_stop (mod_ctx_t p);
void mod_destroy (mod_ctx_t p);


/* Accessors.
 */
const char *mod_name (mod_ctx_t p);
const char *mod_uuid (mod_ctx_t p);
const char *mod_digest (mod_ctx_t p);
int mod_size (mod_ctx_t p);
void *mod_sock (mod_ctx_t p);

#endif /* !_BROKER_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
