#ifndef _BROKER_SERVICE_H
#define _BROKER_SERVICE_H

typedef struct svc_struct svc_t;
typedef struct svchash_struct svchash_t;
typedef int (*svc_cb_f)(const flux_msg_t *msg, void *arg);

svchash_t *svchash_create (void);
void svchash_destroy (svchash_t *sh);

svc_t *svc_add (svchash_t *sh, const char *name, const char *alias,
                svc_cb_f cb, void *arg);
void svc_remove (svchash_t *sh, const char *name);

int svc_sendmsg (svchash_t *sh, const flux_msg_t *msg);

#endif /* !_BROKER_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
