typedef int (*mapstrfun_t) (char *s, void *arg1, void *arg2);

void oom (void);
void *xzmalloc (size_t size);
char *xstrdup (const char *s);
void xgettimeofday (struct timeval *tv, struct timezone *tz);
int env_getint (char *name, int dflt);
char *env_getstr (char *name, char *dflt);
int env_getints (char *name, int **iap, int *lenp, int dflt_ia[], int dflt_len);
int getints (char *s, int **iap, int *lenp);
int mapstr (char *s, mapstrfun_t fun, void *arg1, void *arg2);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
