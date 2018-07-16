/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <argz.h>
#include <czmq.h>

#include "pmi.h"
#include "wrap.h"

#include "src/common/libutil/iterators.h"

struct pmi_wrap {
    void *dso;
};

static int pmi_wrap_init (void *impl, int *spawned)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Init");
    return f ? f (spawned) : PMI_FAIL;
}

static int pmi_wrap_initialized (void *impl, int *initialized)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Initialized");
    return f ? f (initialized) : PMI_FAIL;
}

static int pmi_wrap_finalize (void *impl)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(void) = dlsym (pmi->dso, "PMI_Finalize");
    return f ? f () : PMI_FAIL;
}

static int pmi_wrap_get_size (void *impl, int *size)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_size");
    return f ? f (size) : PMI_FAIL;
}

static int pmi_wrap_get_rank (void *impl, int *rank)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_rank");
    return f ? f (rank) : PMI_FAIL;
}

static int pmi_wrap_get_universe_size (void *impl, int *size)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_universe_size");
    return f ? f (size) : PMI_FAIL;
}

static int pmi_wrap_get_appnum (void *impl, int *appnum)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_appnum");
    return f ? f (appnum) : PMI_FAIL;
}

static int pmi_wrap_barrier (void *impl)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(void) = dlsym (pmi->dso, "PMI_Barrier");
    return f ? f () : PMI_FAIL;
}

static int pmi_wrap_abort (void *impl, int exit_code, const char *error_msg)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int, const char *) = dlsym (pmi->dso, "PMI_Abort");
    return f ? f (exit_code, error_msg) : PMI_FAIL;
}

static int pmi_wrap_kvs_get_my_name (void *impl, char *kvsname, int length)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(char *, int) = dlsym (pmi->dso, "PMI_KVS_Get_my_name");
    return f ? f (kvsname, length) : PMI_FAIL;
}

static int pmi_wrap_kvs_get_name_length_max (void *impl, int *length)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_name_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int pmi_wrap_kvs_get_key_length_max (void *impl, int *length)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_key_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int pmi_wrap_kvs_get_value_length_max (void *impl, int *length)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_value_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int pmi_wrap_kvs_put (void *impl, const char *kvsname,
                             const char *key, const char *value)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *, const char *, const char *) = dlsym (pmi->dso,
                                                                "PMI_KVS_Put");
    return f ? f (kvsname, key, value) : PMI_FAIL;
}

static int pmi_wrap_kvs_commit (void *impl, const char *kvsname)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *) = dlsym (pmi->dso, "PMI_KVS_Commit");
    return f ? f (kvsname) : PMI_FAIL;
}

static int pmi_wrap_kvs_get (void *impl, const char *kvsname,
                             const char *key, char *value, int len)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *, const char *, char *, int) = dlsym (pmi->dso,
                                                               "PMI_KVS_Get");
    return f ? f (kvsname, key, value, len) : PMI_FAIL;
}

static int pmi_wrap_get_clique_size (void *impl, int *size)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_clique_size");
    return f ? f (size) : PMI_FAIL;
}

static int pmi_wrap_get_clique_ranks (void *impl, int *ranks, int length)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int *, int) = dlsym (pmi->dso, "PMI_Get_clique_ranks");
    return f ? f (ranks, length) : PMI_FAIL;
}

static int pmi_wrap_publish_name (void *impl,
                                  const char *service_name, const char *port)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *, const char *) = dlsym (pmi->dso, "PMI_Publish_name");
    return f ? f (service_name, port) : PMI_FAIL;
}

static int pmi_wrap_unpublish_name (void *impl, const char *service_name)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *) = dlsym (pmi->dso, "PMI_Unpublish_name");
    return f ? f (service_name) : PMI_FAIL;
}

static int pmi_wrap_lookup_name (void *impl,
                                 const char *service_name, char *port)
{
    struct pmi_wrap *pmi = impl;
    int (*f)(const char *, const char *) = dlsym (pmi->dso, "PMI_Lookup_name");
    return f ? f (service_name, port) : PMI_FAIL;
}

static int pmi_wrap_spawn_multiple (void *impl,
                                    int count,
                                    const char *cmds[],
                                    const char **argvs[],
                                    const int maxprocs[],
                                    const int info_keyval_sizesp[],
                                    const PMI_keyval_t *info_keyval_vectors[],
                                    int preput_keyval_size,
                                    const PMI_keyval_t preput_keyval_vector[],
                                    int errors[])
{
    struct pmi_wrap *pmi = impl;
    int (*f)(int, const char **, const char ***, const int *, const int *,
             const PMI_keyval_t **, int, const PMI_keyval_t *, int *);
    if ((f = dlsym (pmi->dso, "PMI_Lookup_name")))
        return f (count, cmds, argvs, maxprocs, info_keyval_sizesp,
                  info_keyval_vectors, preput_keyval_size, preput_keyval_vector,
                  errors);
    return PMI_FAIL;
}


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

static void liblist_destroy (zlist_t *libs)
{
    char *entry;
    if (libs) {
        while ((entry = zlist_pop (libs)))
            free (entry);
        zlist_destroy (&libs);
    }
}

static zlist_t *liblist_create (const char *libname)
{
    zlist_t *libs = NULL;

    if (!libname)
        libname = "libpmi.so";
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


static void pmi_wrap_destroy (void *impl)
{
    struct pmi_wrap *pmi = impl;
    if (pmi) {
        if (pmi->dso)
            dlclose (pmi->dso);
        free (pmi);
    }
}

static struct pmi_operations pmi_wrap_operations = {
    .init                       = pmi_wrap_init,
    .initialized                = pmi_wrap_initialized,
    .finalize                   = pmi_wrap_finalize,
    .get_size                   = pmi_wrap_get_size,
    .get_rank                   = pmi_wrap_get_rank,
    .get_appnum                 = pmi_wrap_get_appnum,
    .get_universe_size          = pmi_wrap_get_universe_size,
    .publish_name               = pmi_wrap_publish_name,
    .unpublish_name             = pmi_wrap_unpublish_name,
    .lookup_name                = pmi_wrap_lookup_name,
    .barrier                    = pmi_wrap_barrier,
    .abort                      = pmi_wrap_abort,
    .kvs_get_my_name            = pmi_wrap_kvs_get_my_name,
    .kvs_get_name_length_max    = pmi_wrap_kvs_get_name_length_max,
    .kvs_get_key_length_max     = pmi_wrap_kvs_get_key_length_max,
    .kvs_get_value_length_max   = pmi_wrap_kvs_get_value_length_max,
    .kvs_put                    = pmi_wrap_kvs_put,
    .kvs_commit                 = pmi_wrap_kvs_commit,
    .kvs_get                    = pmi_wrap_kvs_get,
    .get_clique_size            = pmi_wrap_get_clique_size,
    .get_clique_ranks           = pmi_wrap_get_clique_ranks,
    .spawn_multiple             = pmi_wrap_spawn_multiple,
    .destroy                    = pmi_wrap_destroy,
};

/* Notes:
 * - Use RTLD_GLOBAL due to issue #432
 */

void *pmi_wrap_create (const char *libname, struct pmi_operations **ops,
                       bool allow_self_wrap)
{
    struct pmi_wrap *pmi = calloc (1, sizeof (*pmi));
    const char *s;
    int debug = 0;
    char *name;
    zlist_t *libs = NULL;

    if (!pmi)
        goto error;
    if ((s = getenv ("FLUX_PMI_DEBUG")))
        debug = strtoul (s, NULL, 0);
    if (!(libs = liblist_create (libname)))
        goto error;
    FOREACH_ZLIST (libs, name) {
        dlerror ();
        if (!(pmi->dso = dlopen (name, RTLD_NOW | RTLD_GLOBAL))) {
            if (debug) {
                char *errstr = dlerror ();
                if (errstr)
                    fprintf (stderr, "%s: %s\n", __FUNCTION__, errstr);
                else
                    fprintf (stderr, "%s: dlopen %s failed\n",
                             __FUNCTION__, name);
            }
        }
        else if (!allow_self_wrap && dlsym (pmi->dso, "flux_pmi_library")) {
            if (debug)
                fprintf (stderr, "%s: skipping %s\n", __FUNCTION__, name);
            dlclose (pmi->dso);
            pmi->dso = NULL;
        }
        else {
            if (debug)
                fprintf (stderr, "%s: using %s\n", __FUNCTION__, name);
            break;
        }
    }
    if (!pmi->dso)
        goto error;
    liblist_destroy (libs);
    *ops = &pmi_wrap_operations;
    return pmi;
error:
    pmi_wrap_destroy (pmi);
    liblist_destroy (libs);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
