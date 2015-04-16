#ifndef _BROKER_SERVICE_H
#define _BROKER_SERVICE_H

typedef int (*svc_cb_t)(zmsg_t **zmsg, void *arg);

typedef struct {
    svc_cb_t cb;
    void *cb_arg;
} svc_t;

typedef struct {
    zhash_t *zh;
} svchash_t;

svchash_t *svchash_create (void);
void svchash_destroy (svchash_t *sh);

void svc_remove (svchash_t *sh, const char *name);
svc_t *svc_add (svchash_t *sh, const char *name, svc_cb_t cb, void *arg);

svc_t *svc_lookup (svchash_t *sh, const char *name);

int svc_sendmsg (svchash_t *sh, zmsg_t **zmsg);

#endif /* !_BROKER_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
