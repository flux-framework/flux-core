typedef int (*mapstrfun_t) (char *s, void *arg1, void *arg2);

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

/* Print out of memory and exit.
 */
void oom (void);

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
