#ifndef _UTIL_JSONUTIL_H
#define _UTIL_JSONUTIL_H

#include <stdbool.h>
#include <json.h>

/* JSON helpers
 * N.B. for get_data(): caller must free returned data if non-NULL.
 */
void util_json_object_add_data (json_object *o, char *name,
                                  uint8_t *dat, int len);
void util_json_object_add_timeval (json_object *o, char *name,
                                   struct timeval *tvp);

int util_json_object_get_data (json_object *o, char *name,
                                 uint8_t **datp, int *lenp);
int util_json_object_get_timeval (json_object *o, char *name,
                                  struct timeval *tvp);

#endif /* !UTIL_JSONUTIL_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
