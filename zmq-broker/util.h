void oom (void);
void *xzmalloc (size_t size);
char *xstrdup (const char *s);
void xgettimeofday (struct timeval *tv, struct timezone *tz);
int env_getint (char *name, int dflt);
char *env_getstr (char *name, char *dflt);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
