#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "util.h"

void oom (void)
{
    fprintf (stderr, "out of memory\n");
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
    if (gettimeofday (tv, tz) < 0) {
        fprintf (stderr, "gettimeofday: %s\n", strerror (errno));
        exit (1);
    }
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
