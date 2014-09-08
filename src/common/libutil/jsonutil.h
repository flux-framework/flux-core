#ifndef _UTIL_JSONUTIL_H
#define _UTIL_JSONUTIL_H

/* Calculate encoded size of JSON object.
 */
int util_json_size (json_object *o);

/* JSON helpers
 * N.B. for get_data(): caller must free returned data if non-NULL.
 */
json_object *util_json_object_new_object (void);
void util_json_object_add_boolean (json_object *o, char *name, bool val);
void util_json_object_add_double (json_object *o, char *name, double n);
void util_json_object_add_int (json_object *o, char *name, int i);
void util_json_object_add_int64 (json_object *o, char *name, int64_t i);
void util_json_object_add_string (json_object *o, char *name, const char *s);
void util_json_object_add_data (json_object *o, char *name,
                                  uint8_t *dat, int len);
void util_json_object_add_timeval (json_object *o, char *name,
                                   struct timeval *tvp);

int util_json_object_get_boolean (json_object *o, char *name, bool *vp);
int util_json_object_get_double (json_object *o, char *name, double *dp);
int util_json_object_get_int (json_object *o, char *name, int *ip);
int util_json_object_get_int64 (json_object *o, char *name, int64_t *ip);
int util_json_object_get_string (json_object *o, char *name, const char **sp);
int util_json_object_get_data (json_object *o, char *name,
                                 uint8_t **datp, int *lenp);
int util_json_object_get_timeval (json_object *o, char *name,
                                  struct timeval *tvp);
int util_json_object_get_int_array (json_object *o, char *name,
                                    int **ap, int *lp);

void util_json_encode (json_object *o, char **zbufp, unsigned int *zlenp);
void util_json_decode (json_object **op, char *zbuf, unsigned int zlen);
bool util_json_match (json_object *o1, json_object *o2);

struct rusage;
json_object *rusage_to_json (struct rusage *ru);

#endif /* !UTIL_JSONUTIL_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
