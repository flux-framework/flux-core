#ifndef _HAVE_CMB_UTIL_H
#define _HAVE_CMB_UTIL_H

#include <time.h>
#include <stdbool.h>
#include <json/json.h>

/* 's' contains a comma-delimited list of integers.
 * Parse and return ints in an array (iap), and its length in lenp.
 * Caller must free.
 */
int getints (char *s, int **iap, int *lenp);

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
char *xstrdup (const char *s);

double monotime_since (struct timespec t0); /* milliseconds */
void monotime (struct timespec *tp);
bool monotime_isset (struct timespec t);

/* Get integer, string, or comma delimited array of ints from the environment
 * by name, or if not set, return (copy in string/int* case) of default arg.
 */
int env_getint (char *name, int dflt);
char *env_getstr (char *name, char *dflt);
int env_getints (char *name, int **iap, int *lenp, int dflt_ia[], int dflt_len);
int setenvf (const char *name, int overwrite, const char *fmt, ...);

/* Return a string with argcv elements space-delimited.  Caller must free.
 */
char *argv_concat (int argc, char *argv[]);

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


#endif /* !_HAVE_CMB_UTIL_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
