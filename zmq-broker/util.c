#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "util.h"
#include "log.h"

void oom (void)
{
    errno = ENOMEM;
    msg_exit ("out of memory");
    exit (1);
}

void *xzmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        oom ();
    memset (new, 0, size);
    return new;
}

char *xstrdup (const char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        oom ();
    return cpy;
}

void xgettimeofday (struct timeval *tv, struct timezone *tz)
{
    if (gettimeofday (tv, tz) < 0)
        err_exit ("gettimeofday");
}

int env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}

char *env_getstr (char *name, char *dflt)
{
    char *ev = getenv (name);
    return ev ? xstrdup (ev) : xstrdup (dflt);
}

static int _strtoia (char *s, int *ia, int ia_len)
{
    char *next;
    int n, len = 0;

    while (*s) {
        n = strtoul (s, &next, 10);
        s = *next == '\0' ? next : next + 1;
        if (ia) {
            if (ia_len == len)
                break;
            ia[len] = n;
        }
        len++;
    }
    return len;
}

int getints (char *s, int **iap, int *lenp)
{
    int len = _strtoia (s, NULL, 0);
    int *ia = malloc (len * sizeof (int));

    if (!ia)
        return -1;

    (void)_strtoia (s, ia, len);
    *lenp = len;
    *iap = ia;
    return 0;
}

int env_getints (char *name, int **iap, int *lenp, int dflt_ia[], int dflt_len)
{
    char *s = getenv (name);
    int *ia;
    int len;

    if (s) {
        if (getints (s, &ia, &len) < 0)
            return -1;
    } else {
        ia = malloc (dflt_len * sizeof (int));
        if (!ia)
            return -1;
        for (len = 0; len < dflt_len; len++)
            ia[len] = dflt_ia[len];
    }
    *lenp = len;
    *iap = ia;
    return 0;
}

int mapstr (char *s, mapstrfun_t fun, void *arg1, void *arg2)
{
    char *cpy = xstrdup (s);
    char *saveptr, *a1 = cpy;
    int rc = 0;

    for (;;) {
        char *name = strtok_r (a1 , ",", &saveptr);
        if (!name)
            break;
        if (fun (name, arg1, arg2) < 0) {
            rc = -1;
            break;
        }
        a1 = NULL;
    }
    free (cpy);
    return rc;
}

char *argv_concat (int argc, char *argv[])
{
    int i, len = 0;
    char *s;

    for (i = 0; i < argc; i++)
        len += strlen (argv[i]) + 1;
    s = xzmalloc (len + 1);
    for (i = 0; i < argc; i++) {
        strcat (s, argv[i]);
        if (i < argc - 1)
            strcat (s, " "); 
    }
    return s; 
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
