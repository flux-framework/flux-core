/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <fnmatch.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

#include <czmq.h>
#include "extensor.h"

struct flux_module_service {
    /* Loaders:  */
    zhash_t *loaders;   /* Registered loaders hashed by name                 */
    zhash_t *extensions;/* Loaders by filename "extension"                   */

    /* Modules:  */
    zhash_t *modules;   /* Current list of modules loaded (by uuid)          */
    zhash_t *names;     /* Current list of module names (first loaded wins)  */
};

struct flux_module_handle  {
    struct flux_module_loader *loader; /* Loader implementation              */
    flux_extensor_t *owner;            /* Pointer back to them that loaded us */
    char *path;                        /* realpath to this module/plugin     */
    zuuid_t *uuid;                     /* uuid (local to this extensor)      */
    unsigned int loaded:1;             /* module "loaded" or not?            */
    unsigned int destroyed:1;          /* module is being destroyed          */
    void *ctx;                         /* loader specific context            */
};


/*****************************************************************************
 *  "base" DSO loader implementation
\*****************************************************************************/

struct dso_loader {
    void *dso;
    char *last_error;
    const char *name;
};

static void dso_destroy (flux_module_t *p)
{
    struct dso_loader *d;
    if (p && (d = flux_module_getctx (p))) {
        if (d->dso) {
            dlclose (d->dso);
            d->dso = NULL;
        }
        free (d->last_error);
        d->name = NULL;
        free (d);
    }
}

static int dso_init (flux_module_t *p, const char *path, int flags)
{
    struct dso_loader *d = calloc (1, sizeof (*d));
    if (!d)
        return -1;
    flux_module_setctx (p, d);
    return (0);
}

static int dso_load (flux_module_t *p)
{
    char **namep;
    struct dso_loader *d = flux_module_getctx (p);
    const char *path = flux_module_path (p);
    if (!d)
        return -1;
    dlerror ();
    if (!(d->dso = dlopen (path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND))) {
        d->last_error = strdup (dlerror ());
        return -1;
    }
    if (!(namep = dlsym (d->dso, "mod_name")) || !*namep) {
        dlclose (d->dso);
        d->dso = NULL;
        d->last_error = strdup ("module does not export mod_name");
        errno = ENOENT;
        return -1;
    }
    d->name = *namep;
    return 0;
}

static int dso_unload (flux_module_t *p)
{
    int rc;
    struct dso_loader *d;
    if (!p || !(d = flux_module_getctx (p)))
        return (-1);
    d->name = NULL;
    rc = dlclose (d->dso);
    d->dso = NULL;
    return (rc);
}

static const char * dso_get_name (flux_module_t *p)
{
    struct dso_loader *d;
    if (!p || !(d = flux_module_getctx (p)))
        return (NULL);
    return d->name;
}

static const char * dso_strerror (flux_module_t *p)
{
    struct dso_loader *d;
    if (!p || !(d = flux_module_getctx (p)))
        return (NULL);
    return d->last_error;
}

static void * dso_lookup (flux_module_t *p, const char *sym)
{
    struct dso_loader *d;
    if (!p || !(d = flux_module_getctx (p)))
        return (NULL);
    return (dlsym (d->dso, sym));
}

static struct flux_module_loader base_dso_loader = {
    .name = "dso",
    .init = dso_init,
    .load = dso_load,
    .unload = dso_unload,
    .lookup = dso_lookup,
    .destroy = dso_destroy,
    .get_name = dso_get_name,
    .strerror = dso_strerror,
    .extensions = { ".so", NULL },
};

/*****************************************************************************
 *  Extensor public implementation:
\*****************************************************************************/

void flux_extensor_destroy (flux_extensor_t *s)
{
    if (s) {
        if (s->loaders)
            zhash_destroy (&s->loaders);
        if (s->extensions)
            zhash_destroy (&s->extensions);
        if (s->modules)
            zhash_destroy (&s->modules);
        if (s->names)
            zhash_destroy (&s->names);
        free (s);
    }
}

flux_extensor_t * flux_extensor_create ()
{
    flux_extensor_t *s = calloc (1, sizeof (*s));
    if (s == NULL)
        return NULL;
    if (!(s->loaders = zhash_new ())
       || !(s->extensions = zhash_new ())
       || !(s->names = zhash_new ())
       || !(s->modules = zhash_new ()))
        goto error;

    /* Register base dso loader implementation:
     */
    if (flux_extensor_register_loader (s, &base_dso_loader) < 0)
        goto error;

    return (s);
error:
    flux_extensor_destroy (s);
    return NULL;
}

int flux_extensor_register_loader (flux_extensor_t *s,
    struct flux_module_loader *l)
{
    char **ep;

    if (!l->name || !l->extensions) {
        errno = EINVAL;
        return -1;
    }
    /*  Last loader registered wins */
    zhash_delete (s->loaders, l->name);
    if (zhash_insert (s->loaders, l->name, l) < 0)
        return -1;

    ep = l->extensions;
    while (ep && (*ep)) {
        zhash_delete (s->extensions, *ep);
        zhash_insert (s->extensions, *ep, l);
        ep++;
    }
    return 0;
}

struct flux_module_loader *
flux_extensor_get_loader (flux_extensor_t *s, const char *name)
{
    return zhash_lookup (s->loaders, name);
}

/* Free a zlist object, used by append/remove module */
static void freelist (void *arg)
{
    zlist_t *l = arg;
    zlist_destroy (&l);
}

/*
 *  Append module `p` onto back of queue for its module name. If a module
 *   already exists with that name, it will take precedence until it is
 *   "removed" at which point next registered name will be at head of queue.
 */
static void extensor_append_module (flux_extensor_t *s, flux_module_t *p)
{
    const char *name = flux_module_name (p);
    zlist_t *l = zhash_lookup (s->names, name);
    if (!l) {
        l = zlist_new ();
        zhash_insert (s->names, name, l);
        zhash_freefn (s->names, name, freelist);
    }
    zlist_append (l, p);
}

/*
 *   Remove module `p` from the queue of names. If it was at head of
 *    queue, a new module will now be used to resolve "name".
 */
static int extensor_remove_module (flux_extensor_t *s, flux_module_t *p)
{
    const char *name = flux_module_name (p);
    zlist_t *l = zhash_lookup (s->names, name);
    if (!l) {
        errno = ENOENT;
        return -1;
    }
    zlist_remove (l, p);
    if (zlist_size (l) == 0) {
        zhash_delete (s->names, name);
    }
    return (0);
}

/*
 *  Get current head module from queue for `name`.
 */
static flux_module_t *
extensor_get_module (flux_extensor_t *s, const char *name)
{
    zlist_t *l = zhash_lookup (s->names, name);
    if (!l) {
        errno = ENOENT;
        return NULL;
    }
    return (flux_module_t *) zlist_head (l);
}

/*
 *  Load all possible modules in directory `dirpath`. If `match` is non-NULL
 *   only load modules with names that match string `match`, and stop at first
 *   match.
 */
static int extensor_loadall (flux_extensor_t *s,
    const char *dirpath, const char *match, int max)
{
    int count = 0;
    int n;
    flux_module_t *p = NULL;
    DIR *dir;
    struct dirent entry, *dent;
    char path [PATH_MAX];
    struct stat sb;
    size_t len = sizeof (path);

    if (!(dir = opendir (dirpath)))
        return -1;
    while ((errno = readdir_r (dir, &entry, &dent) == 0) && dent != NULL) {
        if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
            continue;
        if (snprintf (path, len, "%s/%s", dirpath, dent->d_name) >= len) {
            errno = EINVAL;
            count = -1;
            break;
        }
        if (stat (path, &sb) < 0)
            continue;
        if (S_ISDIR (sb.st_mode)) {
            if ((n = extensor_loadall (s, path, match, max)) < 0) {
                count = -1;
                break;
            }
            else
                count += n;
            if (max && count == max)
                break;
        }
        else if ((p = flux_module_create (s, path, 0))) {
            if (flux_module_load (p) < 0) {
                flux_module_destroy (p);
                continue;
            }
            /*
             *  If match is required, check it here. Unload module if there
             *   is no match, otherwise return immediately.
             */
            if (match && (fnmatch (match, flux_module_name (p), 0) != 0)) {
                flux_module_destroy (p);
                continue;
            }
            ++count;
            if (max && count == max)
                break;
        }
    }
    closedir (dir);
    return (count);
}

static int extensor_search (flux_extensor_t *s, const char *searchpath,
    const char *pattern, int maxresults)
{
    int count = 0;
    int n;
    char *cpy = NULL;
    char *dirpath, *saveptr = NULL, *a1 = NULL;

    cpy = a1 = strdup (searchpath);
    if (cpy == NULL)
        return (-1);

    while ((dirpath = strtok_r (a1, ":", &saveptr))) {
        if ((n = extensor_loadall (s, dirpath, pattern, maxresults)) < 0) {
            count = -1;
            break;
        }
        count += n;
        a1 = NULL;
    }
    free (cpy);
    return (count);
}

int flux_extensor_loadall (flux_extensor_t *s, const char *searchpath)
{
    return extensor_search (s, searchpath, NULL, 0);
}

flux_module_t *
flux_extensor_get_module (flux_extensor_t *s, const char *name)
{
    return extensor_get_module (s, name);
}

static flux_module_t * extensor_load (flux_extensor_t *s, const char *arg)
{
    flux_module_t * m = flux_module_create (s, arg, 0);
    if (!m || (flux_module_load (m) < 0)) {
        flux_module_destroy (m);
        return (NULL);
    }
    return (m);
}

flux_module_t * flux_extensor_load_module (flux_extensor_t *s,
    const char *searchpath, const char *arg)
{
    if (strchr (arg, '/'))
        return extensor_load (s, arg);
    else if (extensor_search (s, searchpath, arg, 1) != 1)
        return NULL;
    return (flux_extensor_get_module (s, arg));
}

/*****************************************************************************
 *  flux_module_t interface:
\*****************************************************************************/

static const char * path_extension (const char *path)
{
    const char *dot = strrchr (path, '.');
    if (!dot || dot == path)
        return "";
    return (dot);
}

static flux_module_t * module_create (flux_extensor_t *s,
    struct flux_module_loader *l,
    const char *path, int flags)
{
    flux_module_t *p = calloc (1, sizeof (*p));
    if (!p)
        return NULL;
    p->owner = s;
    p->loader = l;
    /*
     *  Attempt to get realpath, but no fatal errror on failure:
     */
    if (!(p->path = realpath (path, NULL)))
        p->path = strdup (path);

    if (p->path == NULL
      || !(p->uuid = zuuid_new ())
      ||  (p->loader->init (p, path, flags) < 0))
        goto fail;

    zhash_insert (s->modules, zuuid_str (p->uuid), p);
    zhash_freefn (s->modules, zuuid_str (p->uuid),
                  (zhash_free_fn *) flux_module_destroy);
    return (p);
fail:
    if (p->uuid)
        zuuid_destroy (&p->uuid);
    free (p->path);
    free (p);
    return NULL;
}

flux_module_t * flux_module_create (flux_extensor_t *s,
    const char *path, int flags)
{
    const char *exe = path_extension (path);
    struct flux_module_loader *l = zhash_lookup (s->extensions, exe);
    if (!l) {
        errno = ENOSYS;
        return NULL;
    }
    return module_create (s, l, path, flags);
}

flux_module_t * flux_module_create_with_loader (flux_extensor_t *s,
    const char *loader_name,
    const char *path, int flags)
{
    struct flux_module_loader *l = zhash_lookup (s->loaders, loader_name);
    if (!l) {
        errno = ENOSYS;
        return NULL;
    }
    return module_create (s, l, path, flags);
}

void * flux_module_getctx (flux_module_t *p)
{
    return p->ctx;
}

void * flux_module_setctx (flux_module_t *p, void *ctx)
{
    void * oldctx = p->ctx;
    p->ctx = ctx;
    return oldctx;
}

int flux_module_load (flux_module_t *p)
{
    if (p->loaded)
        return (0);

    if (p->loader->load (p) < 0)
        return (-1);
    p->loaded = 1;
    /*
     *  Link this module under its returned name, if there is no other
     *   module by this name already.
     */
    extensor_append_module (p->owner, p);
    return (0);
}

int flux_module_unload (flux_module_t *p)
{
    if (p->loaded) {
        /*
         *  Always remove module name from extensor names hash
         *   *before* calling `unload` implementation. Module name
         *   may not be available after unload!
         */
        extensor_remove_module (p->owner, p);
        if (p->loader->unload (p) < 0)
            return -1;
        p->loaded = 0;
    }
    return (0);
}

void * flux_module_lookup (flux_module_t *p, const char *sym)
{
    return p->loader->lookup (p, sym);
}

const char * flux_module_strerror (flux_module_t *p)
{
    return p->loader->strerror (p);
}

const char * flux_module_name (flux_module_t *p)
{
    return p->loader->get_name (p);
}

const char * flux_module_path (flux_module_t *p)
{
    return p->path;
}

const char * flux_module_uuid (flux_module_t *p)
{
    return zuuid_str (p->uuid);
}

void flux_module_destroy (flux_module_t *p)
{
    if (p->destroyed)
        return;

    p->destroyed = 1;
    flux_module_unload (p);

    /*  Remove from service module hash (ignore error) */
    if (p->uuid && p->owner)
        zhash_delete (p->owner->modules, zuuid_str (p->uuid));

    /*  Call loader destroy method */
    if (p->loader && p->loader->destroy)
        p->loader->destroy (p);
    p->ctx = NULL;

    /*  Free remaining components */
    if (p->uuid)
        zuuid_destroy (&p->uuid);
    free (p->path);
    free (p);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
