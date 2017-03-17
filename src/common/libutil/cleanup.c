/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include "cleanup.h"
#include "xzmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#include <pthread.h>

#include <czmq.h>

#ifndef O_PATH
#define O_PATH 0 /* O_PATH is too new for CentOS 6 - see issue #646 */
#endif

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct cleaner {
    cleaner_fun_f * fun;
    void * arg;
};

static void unlinkat_recursive (int fd)
{
    DIR *dir;
    struct dirent *d, entry;
    struct stat sb;
    int cfd;

    if ((dir = fdopendir (fd))) {
        while (readdir_r (dir, &entry, &d) == 0 && d != NULL) {
            if (!strcmp (d->d_name, ".") || !strcmp (d->d_name, ".."))
                continue;
            if (fstatat (fd, d->d_name, &sb, AT_SYMLINK_NOFOLLOW) < 0)
                continue;
            if (S_ISDIR (sb.st_mode)) {
                if ((cfd = openat (fd, d->d_name, O_PATH | O_DIRECTORY)) < 0)
                    continue;
                unlinkat_recursive (cfd);
                (void)unlinkat (fd, d->d_name, AT_REMOVEDIR);
            } else
                (void)unlinkat (fd, d->d_name, 0);
        }
        closedir (dir);
    }
}

static void unlink_recursive (const char *dirpath)
{
    DIR *dir = opendir (dirpath);
    if (dir) {
        unlinkat_recursive (dirfd (dir));
        (void)rmdir (dirpath);
    }
}

void cleanup_directory_recursive (const struct cleaner *c)
{
    if (c && c->arg)
        unlink_recursive (c->arg);
}

void cleanup_directory (const struct cleaner *c)
{
    if(c && c->arg)
        rmdir(c->arg);
}

void cleanup_file (const struct cleaner *c)
{
    if(c && c->arg)
        unlink(c->arg);
}

static pid_t cleaner_pid = 0;
static zlist_t *cleanup_list = NULL;
void cleanup_run (void)
{
    struct cleaner *c;
    pthread_mutex_lock(&mutex);
    if ( ! cleanup_list || cleaner_pid != getpid())
        goto out;
    c = zlist_first(cleanup_list);
    while (c){
        if (c && c->fun){
            c->fun(c);
        }
        if (c->arg)
            free (c->arg);
        free (c);
        c = zlist_next(cleanup_list);
    }
    zlist_destroy(&cleanup_list);
    cleanup_list = NULL;
out:
    pthread_mutex_unlock(&mutex);
}

void cleanup_push (cleaner_fun_f *fun, void * arg)
{
    pthread_mutex_lock(&mutex);
    if (! cleanup_list || cleaner_pid != getpid())
    {
        // This odd dance is to handle forked processes that do not exec
        if (cleaner_pid != 0 && cleanup_list) {
            zlist_destroy(&cleanup_list);
        }
        cleanup_list = zlist_new();
        cleaner_pid = getpid();
        atexit (cleanup_run);
    }
    struct cleaner * c = calloc(sizeof(struct cleaner), 1);
    c->fun = fun;
    c->arg = arg;
    /* Ignore return code, no way to return it callery anyway... */
    (void) zlist_push(cleanup_list, c);
    pthread_mutex_unlock(&mutex);
}

void cleanup_push_string (cleaner_fun_f *fun, const char * path)
{
    cleanup_push(fun, xstrdup(path));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
