/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dirwalk.c - simple interface to recurse a directory tree */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>
#include <fnmatch.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "basename.h"
#include "dirwalk.h"

struct direntry {
    int close_dirfd;
    int dirfd;
    char *path;
    const char *basename;
    struct stat sb;
};

struct dirwalk {
    int flags;
    int count;
    zlist_t *dirstack;
    struct direntry *current;

    zlist_t *results;

    unsigned int stopped:1;
    int errnum;
};

static void direntry_destroy (struct direntry *e)
{
    if (e) {
        if (e->close_dirfd)
            close (e->dirfd);
        e->dirfd = -1;
        e->basename = NULL;
        free (e->path);
        free (e);
    }
}

/*
 *  Create a direntry under parent dirfd `fd` and path `dir`, from
 *   directory entry `dent`.
 */
static struct direntry *direntry_create (int fd, const char *dir,
                                         struct dirent *dent)
{
    struct direntry *e = calloc (1, sizeof (*e));
    if (!e)
        return NULL;
    if (asprintf (&e->path, "%s/%s", dir, dent->d_name) < 0)
        goto out_err;
    if (fstatat (fd, dent->d_name, &e->sb, AT_SYMLINK_NOFOLLOW) < 0)
        goto out_err;
    e->dirfd = fd;
    return (e);
out_err:
    direntry_destroy (e);
    return NULL;
}

/*
 *  Create a direntry for an initial dirpath. Force open the parent dirfd
 *   from `dir/..` for this special object.
 */
static struct direntry *direntry_create_dir (const char *dirpath)
{
    char *parent;
    struct direntry *e = calloc (1, sizeof (*e));
    if (!e)
        return NULL;
    e->path = strdup (dirpath);
    if (asprintf (&parent, "%s/..", dirpath) < 0)
        goto out_err;
    if ((e->dirfd = open (parent, O_DIRECTORY|O_RDONLY)) < 0)
        goto out_err;
    e->close_dirfd = 1;

    /*
     *  No need to use fstatat(2) here. This function is never called after
     *   a check, so there should be no TOU-TOC issues here.
     */
    if (stat (dirpath, &e->sb) < 0)
        goto out_err;

    free (parent);
    return e;
out_err:
    free (parent);
    direntry_destroy (e);
    return NULL;
}

static void dirwalk_destroy (dirwalk_t *d)
{
    if (d) {
        if (d->current)
            direntry_destroy (d->current);
        if (d->dirstack) {
            struct direntry *e;
            while ((e = zlist_pop (d->dirstack)))
                direntry_destroy (e);
            zlist_destroy (&d->dirstack);
        }
        free (d);
    }
}


static dirwalk_t *dirwalk_create ()
{
    dirwalk_t *d = calloc (1, sizeof (*d));
    if (!d || !(d->dirstack = zlist_new ())) {
        dirwalk_destroy (d);
        return NULL;
    }
    return (d);
}

static int dirwalk_set_flags (dirwalk_t *d, int flags)
{
    int old = d->flags;
    d->flags = flags;
    return old;
}

const char * dirwalk_name (dirwalk_t *d)
{
    if (!d->current)
        return NULL;
    if (!d->current->basename)
        d->current->basename = basename_simple (d->current->path);
    return d->current->basename;
}

const char * dirwalk_path (dirwalk_t *d)
{
    if (!d->current)
        return NULL;
    return d->current->path;
}

const struct stat * dirwalk_stat (dirwalk_t *d)
{
    if (!d->current)
        return NULL;
    return &d->current->sb;
}

int dirwalk_dirfd (dirwalk_t *d)
{
    if (!d->current)
        return -1;
    return d->current->dirfd;
}

int dirwalk_isdir (dirwalk_t *d)
{
    if (!d->current)
        return 0;
    return S_ISDIR (d->current->sb.st_mode);
}

static int is_dotted_dir (struct dirent *dent)
{
    if (streq (dent->d_name, ".") || streq (dent->d_name, ".."))
        return 1;
    return 0;
}

static void dirwalk_visit (dirwalk_t *d, dirwalk_filter_f fn, void *arg)
{
    if (fn)
        (*fn) (d, arg);
    d->count++;
}

/*
 *  Continue traversal of d->current, which must be a directory
 */
static int dirwalk_traverse (dirwalk_t *d, dirwalk_filter_f fn, void *arg)
{
    int fd;
    const char *path;
    struct dirent *dent;
    DIR *dirp = NULL;

    assert (dirwalk_isdir (d));
    path = d->current->path;

    if (!(dirp = opendir (path)))
        return -1;

    if ((fd = dirfd (dirp)) < 0)
        goto done;

    /*
     * Visit this directory if not depth-first
     */
    if (!(d->flags & DIRWALK_DEPTH))
        dirwalk_visit (d, fn, arg);

    zlist_push (d->dirstack, d->current);
    while ((dent = readdir (dirp)) && !d->stopped) {
        if (is_dotted_dir (dent))
            continue;
        if (!(d->current = direntry_create (fd, path, dent))) {
            if (errno == ENOMEM)
                dirwalk_stop (d, errno);
            continue;
        }
        if (S_ISDIR (d->current->sb.st_mode)
            && !(d->flags & DIRWALK_NORECURSE)) {
            /*
             *  Save current direntry onto stack and call traverse()
             */
            zlist_push (d->dirstack, d->current);
            (void) dirwalk_traverse (d, fn, arg);
            d->current = zlist_pop (d->dirstack);
        }
        else /* Not a directory or NORECURSE, simply visit this object */
            dirwalk_visit (d, fn, arg);
        direntry_destroy (d->current);
        d->current = NULL;
    }
    d->current = zlist_pop (d->dirstack);

    /*
     *  Visit directory if !stopped and not depth-first
     */
    if (!d->stopped && (d->flags & DIRWALK_DEPTH))
        dirwalk_visit (d, fn, arg);

done:
    closedir (dirp);
    if (d->errnum) {
        errno = d->errnum;
        return -1;
    }
    return 0;
}

void dirwalk_stop (dirwalk_t *d, int errnum)
{
    d->stopped = 1;
    d->errnum = errnum;
}

int dirwalk (const char *path, int flags,
             dirwalk_filter_f fn, void *arg)
{
    char *dirpath = NULL;
    int count = -1;
    dirwalk_t *d = dirwalk_create ();
    if (!d)
        return -1;
    /*
     *  If user requested realpaths then resolve path argument
     *   using realpath(3) here. This should result in *all*
     *   following paths being absolute realpaths as well.
     */
    if ((flags & DIRWALK_REALPATH)) {
        if (!(dirpath = realpath (path, NULL)))
            goto out;
        path = dirpath;
    }
    /*
     *  Bootstrap traversal by pushing flags into dirwalk object,
     *   then force "current" dir to the path at which we want to
     *   start traversal.
     */
    if ((dirwalk_set_flags (d, flags) < 0)
     || !(d->current = direntry_create_dir (path))
     || (dirwalk_traverse (d, fn, arg) < 0))
        goto out;
    count = d->count;
out:
    free (dirpath);
    dirwalk_destroy (d);
    return count;
}

struct find_arg {
    int count;
    const char *pattern;
    zlist_t *results;
    dirwalk_filter_f fn;
    void *arg;
};

static int find_f (dirwalk_t *d, void *arg)
{
    struct find_arg *a = arg;
    /* Skip directories unless DIRWALK_FIND_DIR was provided */
    if (!(d->flags & DIRWALK_FIND_DIR) && dirwalk_isdir (d))
        return 0;

    if (fnmatch (a->pattern, dirwalk_name (d), 0) == 0) {
        if (a->fn && ((*a->fn) (d, a->arg) <= 0))
            return 0;
        zlist_append (a->results, (char *) dirwalk_path (d));
        if (a->count && zlist_size (a->results) == a->count)
            dirwalk_stop (d, 0);
    }
    return 0;
}

zlist_t *dirwalk_find (const char *searchpath, int flags,
                       const char *pattern, int count,
                       dirwalk_filter_f fn, void *uarg)
{
    int saved_errno;
    char *copy = NULL;
    char *s, *dirpath, *sptr = NULL;
    struct find_arg arg = { .count = count, .pattern = pattern,
                            .fn = fn, .arg = uarg };

    if (!(arg.results = zlist_new ()))
        return NULL;
    zlist_autofree (arg.results);

    if (!(copy = strdup (searchpath)))
        goto err;

    s = copy;
    while ((dirpath = strtok_r (s, ":", &sptr))) {
        if ((dirwalk (dirpath, flags, find_f, &arg) < 0)) {
            /* Ignore missing and inaccessible paths */
            if ((errno != ENOENT) && (errno != EACCES))
                goto err;
        }
        else if (count && zlist_size (arg.results) == count)
            break;
        s = NULL;
    }
    free (copy);
    return arg.results;
err:
    saved_errno = errno;
    if (arg.results)
        zlist_destroy (&arg.results);
    free (copy);
    errno = saved_errno;
    return NULL;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
