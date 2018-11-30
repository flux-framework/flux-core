#ifndef _BROKER_SERVICE_H
#define _BROKER_SERVICE_H

#include <jansson.h>

typedef int (*service_send_f)(const flux_msg_t *msg, void *arg);

struct service_switch *service_switch_create (void);
void service_switch_destroy (struct service_switch *sw);

int service_add (struct service_switch *sw, const char *name,
                 const char *uuid, service_send_f cb, void *arg);

void service_remove (struct service_switch *sw, const char *name);

void service_remove_byuuid (struct service_switch *sw, const char *uuid);

int service_send (struct service_switch *sw, const flux_msg_t *msg);

/* Return the UUID currently registered for service `name` */
const char *service_get_uuid (struct service_switch *sw, const char *name);

json_t *service_list_byuuid (struct service_switch *sw, const char *uuid);

#endif /* !_BROKER_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
