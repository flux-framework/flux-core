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
#    include <config.h>
#endif /* HAVE_CONFIG_H */

#include "cleanup.h"
#include "xzmalloc.h"
#include "unlink_recursive.h"

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <pthread.h>

#include <czmq.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct cleaner {
    cleaner_fun_f *fun;
    void *arg;
};

void cleanup_directory_recursive (const struct cleaner *c)
{
    if (c && c->arg)
        unlink_recursive (c->arg);
}

void cleanup_directory (const struct cleaner *c)
{
    if (c && c->arg)
        rmdir (c->arg);
}

void cleanup_file (const struct cleaner *c)
{
    if (c && c->arg)
        unlink (c->arg);
}

static pid_t cleaner_pid = 0;
static zlist_t *cleanup_list = NULL;
void cleanup_run (void)
{
    struct cleaner *c;
    pthread_mutex_lock (&mutex);
    if (!cleanup_list || cleaner_pid != getpid ())
        goto out;
    c = zlist_first (cleanup_list);
    while (c) {
        if (c && c->fun) {
            c->fun (c);
        }
        if (c->arg)
            free (c->arg);
        free (c);
        c = zlist_next (cleanup_list);
    }
    zlist_destroy (&cleanup_list);
    cleanup_list = NULL;
out:
    pthread_mutex_unlock (&mutex);
}

void cleanup_push (cleaner_fun_f *fun, void *arg)
{
    pthread_mutex_lock (&mutex);
    if (!cleanup_list || cleaner_pid != getpid ()) {
        // This odd dance is to handle forked processes that do not exec
        if (cleaner_pid != 0 && cleanup_list) {
            zlist_destroy (&cleanup_list);
        }
        cleanup_list = zlist_new ();
        cleaner_pid = getpid ();
        atexit (cleanup_run);
    }
    struct cleaner *c = calloc (sizeof (struct cleaner), 1);
    c->fun = fun;
    c->arg = arg;
    /* Ignore return code, no way to return it callery anyway... */
    (void)zlist_push (cleanup_list, c);
    pthread_mutex_unlock (&mutex);
}

void cleanup_push_string (cleaner_fun_f *fun, const char *path)
{
    cleanup_push (fun, xstrdup (path));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
