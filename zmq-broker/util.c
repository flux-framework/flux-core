#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

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

char *xstrdup (char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        oom ();
    return cpy;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
