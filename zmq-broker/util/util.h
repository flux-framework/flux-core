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

/* Return a string with argcv elements space-delimited.  Caller must free.
 */
char *argv_concat (int argc, char *argv[]);

/* Fill 'href' with ASCII string representation of SHA1 hash of dat/len.
 * The other two compute and verify same over serialized JSON object.
 */
void compute_href (const void *dat, int len, href_t href);
void compute_json_href (json_object *o, href_t href);
bool verify_json_href (json_object *o, const href_t href);


/* JSON helpers
 * N.B. for get_base64(): caller must free returned data if non-NULL.
 */
json_object *util_json_object_new_object (void);
json_object *util_json_object_dup (json_object *o);
void util_json_object_add_boolean (json_object *o, char *name, bool val);
void util_json_object_add_double (json_object *o, char *name, double n);
void util_json_object_add_int (json_object *o, char *name, int i);
void util_json_object_add_string (json_object *o, char *name, const char *s);
void util_json_object_add_base64 (json_object *o, char *name,
                                  uint8_t *dat, int len);
void util_json_object_add_timeval (json_object *o, char *name,
                                   struct timeval *tvp);

int util_json_object_get_boolean (json_object *o, char *name, bool *vp);
int util_json_object_get_double (json_object *o, char *name, double *dp);
int util_json_object_get_int (json_object *o, char *name, int *ip);
int util_json_object_get_string (json_object *o, char *name, const char **sp);
int util_json_object_get_base64 (json_object *o, char *name,
                                 uint8_t **datp, int *lenp);
int util_json_object_get_timeval (json_object *o, char *name,
                                  struct timeval *tvp);
int util_json_object_get_int_array (json_object *o, char *name,
                                    int **ap, int *lp);

json_object *util_json_vlog (int level, const char *fac, const char *src,
                             const char *fmt, va_list ap);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
