/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <argz.h>

#include "liblist.h"

static int liblist_append_from_environment (zlist_t *libs, const char *libname)
{
    const char *path;
    char *argz = NULL;
    size_t argz_len;
    int rc = -1;
    char *filename, *entry = NULL;

    if ((path = getenv ("LD_LIBRARY_PATH"))) {
        if (argz_create_sep (path, ':', &argz, &argz_len) != 0)
            goto done;
        while ((entry = argz_next (argz, argz_len, entry))) {
            if (asprintf (&filename, "%s/%s", entry, libname) < 0)
                goto done;
            if (access (filename, F_OK) < 0) {
                free (filename);
                continue;
            }
            if (zlist_append (libs, filename) < 0) {
                free (filename);
                goto done;
            }
        }
    }
    rc = 0;
done:
    if (argz)
        free (argz);
    return rc;
}

static int split2 (char *s, int delim, char **w1, char **w2)
{
    char *p = strchr (s, delim);
    if (!p)
        return -1;
    *p++ = '\0';
    *w1 = s;
    *w2 = p;
    return 0;
}

static void trim_end (char *s, int ch)
{
    int len = strlen (s);
    while (len > 0) {
        if (s[len - 1] != ch)
            break;
        s[--len] = '\0';
    }
}

static int liblist_append_from_ldconfig (zlist_t *libs, const char *libname)
{
    FILE *f;
    const char *cmd = "ldconfig -p | sed -e 's/([^(]*)[\t ]*//'" \
                      "            | awk -F\" => \" '{print $1 \":\" $2};'";
    char line[1024];
    int rc = -1;

    if (!(f = popen (cmd, "r")))
        goto done;
    while  (fgets (line, sizeof (line), f) != NULL) {
        char *name, *path, *cpy;
        if (split2 (line, ':', &name, &path) < 0)
            continue;
        while (isspace (*name))
            name++;
        if (strcmp (name, libname) != 0)
            continue;
        trim_end (path, '\n');
        if (!(cpy = strdup (path)))
            goto done;
        if (zlist_append (libs, cpy) < 0) {
            free (cpy);
            goto done;
        }
    }
    rc = 0;
done:
    if (f)
        fclose (f);
    return rc;
}

void liblist_destroy (zlist_t *libs)
{
    char *entry;
    if (libs) {
        while ((entry = zlist_pop (libs)))
            free (entry);
        zlist_destroy (&libs);
    }
}

zlist_t *liblist_create (const char *libname)
{
    zlist_t *libs = NULL;

    if (!(libs = zlist_new ()))
        goto error;
    if (strchr (libname, '/')) {
        char *cpy = strdup (libname);
        if (!cpy)
            goto error;
        if (zlist_append (libs, cpy) < 0) {
            free (cpy);
            goto error;
        }
    } else {
        if (liblist_append_from_environment (libs, libname) < 0)
            goto error;
        if (liblist_append_from_ldconfig (libs, libname) < 0)
            goto error;
    }
    return libs;
error:
    liblist_destroy (libs);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth
 */
