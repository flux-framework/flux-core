#ifndef _BROKER_SERVICE_H
#define _BROKER_SERVICE_H

typedef int (*service_send_f)(const flux_msg_t *msg, void *arg);

struct service_switch *service_switch_create (void);
void service_switch_destroy (struct service_switch *sw);

int service_add (struct service_switch *sw,
                 const char *name, const char *alias,
                 service_send_f cb, void *arg);
void service_remove (struct service_switch *sw,
                     const char *name);

int svc_sendmsg (struct service_switch *sw, const flux_msg_t *msg);

#endif /* !_BROKER_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
