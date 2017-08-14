#ifndef _BROKER_SERVICE_H
#define _BROKER_SERVICE_H

typedef int (*svc_cb_f)(const flux_msg_t *msg, void *arg);

struct service_switch *service_switch_create (void);
void service_switch_destroy (struct service_switch *sw);

int svc_add (struct service_switch *sw,
             const char *name, const char *alias,
             svc_cb_f cb, void *arg);
void svc_remove (struct service_switch *sw,
                 const char *name);

int svc_sendmsg (struct service_switch *sw, const flux_msg_t *msg);

#endif /* !_BROKER_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
