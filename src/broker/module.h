#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

/* Templates used to construct module socket URIs
 */
#define MODREQUEST_INPROC_URI         "inproc://request"
#define MODEVENT_INPROC_URI           "inproc://event"
#define SVC_INPROC_URI_TMPL           "inproc://svc-%s"

/* Create, start, stop, destroy a module.
 * Termination:  mod_stop (), read EOF on sock, mod_destroy ()
 */
typedef struct mod_ctx_struct *mod_ctx_t;
mod_ctx_t mod_create (zctx_t *zctx, uint32_t rank, const char *path);
void mod_set_args (mod_ctx_t p, int argc, char * const argv[]);
void mod_add_arg (mod_ctx_t p, const char *arg);
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
