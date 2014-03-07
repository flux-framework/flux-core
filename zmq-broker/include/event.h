#ifndef FLUX_EVENT_H
#define FLUX_EVENT_H

/* For direct interaction with event module.
 * In most cases flux.h event functions should be used.
 */
int flux_event_pub (flux_t h, const char *topic, json_object *payload);
int flux_event_geturi (flux_t h, char **urip);

#endif
