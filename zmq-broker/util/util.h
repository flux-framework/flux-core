#ifndef _HAVE_CMB_UTIL_H
#define _HAVE_CMB_UTIL_H
typedef int (*mapstrfun_t) (char *s, void *arg1, void *arg2);

typedef char href_t[41];

/* 's' contains a comma-delimited list.
 * Call 'fun' once for each word in the list.
 * arg1 and arg2 will be passed opaquely to 'fun'.
 */
int mapstr (char *s, mapstrfun_t fun, void *arg1, void *arg2);

/* 's' contains a comma-delimited list of integers.
 * Parse and return ints in an array (iap), and its length in lenp.
 * Caller must free.
 */
int getints (char *s, int **iap, int *lenp);

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
char *xstrdup (const char *s);

/* A gettimeofday that exits on error.
 */
void xgettimeofday (struct timeval *tv, struct timezone *tz);

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

/* Generate a UUID string.  Caller must free.
 */
char *uuid_generate_str (void);

/* Fill 'href' with ASCII SHA1 hash of serialized JSON object.
 */
void compute_json_href (json_object *o, href_t href);

/* JSON helpers
 * N.B. for get_base64(): caller must free returned data if non-NULL.
 */
json_object *util_json_object_new_object (void);
void util_json_object_add_boolean (json_object *o, char *name, bool val);
void util_json_object_add_double (json_object *o, char *name, double n);
void util_json_object_add_int (json_object *o, char *name, int i);
void util_json_object_add_int64 (json_object *o, char *name, int64_t i);
void util_json_object_add_string (json_object *o, char *name, const char *s);
void util_json_object_add_base64 (json_object *o, char *name,
                                  uint8_t *dat, int len);
void util_json_object_add_timeval (json_object *o, char *name,
                                   struct timeval *tvp);

int util_json_object_get_boolean (json_object *o, char *name, bool *vp);
int util_json_object_get_double (json_object *o, char *name, double *dp);
int util_json_object_get_int (json_object *o, char *name, int *ip);
int util_json_object_get_int64 (json_object *o, char *name, int64_t *ip);
int util_json_object_get_string (json_object *o, char *name, const char **sp);
int util_json_object_get_base64 (json_object *o, char *name,
                                 uint8_t **datp, int *lenp);
int util_json_object_get_timeval (json_object *o, char *name,
                                  struct timeval *tvp);
int util_json_object_get_int_array (json_object *o, char *name,
                                    int **ap, int *lp);

void util_json_encode (json_object *o, char **zbufp, unsigned int *zlenp);
void util_json_decode (json_object **op, char *zbuf, unsigned int zlen);
bool util_json_match (json_object *o1, json_object *o2);

json_object *util_json_vlog (int level, const char *fac, const char *src,
                             const char *fmt, va_list ap);


#endif /* !_HAVE_CMB_UTIL_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
