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

/* tkvswatch.c - exercise kvs watch functions */

/* Usage ./tkvswatch nthreads changes key
 * Spawn 'nthreads' threads each watching the same value.
 * Change it 'changes' times and ensure that at minimum the last
 * value is read.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#define USE_REACTOR 1

int changes = -1;
int nthreads = -1;
char *key = NULL;
char *key_stable = NULL;

static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;
static int start_count = 0;


typedef struct {
    pthread_t tid;
    pthread_attr_t attr;
    int n;
    flux_t h;
    int change_count;
    int nil_count;
    int stable_count;
    int last_val;
} thd_t;

static void signal_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        errn_exit (rc, "pthread_mutex_lock");
    start_count++;
    if ((rc = pthread_mutex_unlock (&start_lock)))
        errn_exit (rc, "pthread_mutex_unlock");
    if ((rc = pthread_cond_signal (&start_cond)))
        errn_exit (rc, "pthread_cond_signal");
}

static void wait_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        errn_exit (rc, "pthread_mutex_lock");
    while (start_count < nthreads) {
        if ((rc = pthread_cond_wait (&start_cond, &start_lock)))
            errn_exit (rc, "pthread_cond_wait");
    }
    if ((rc = pthread_mutex_unlock (&start_lock)))
        errn_exit (rc, "pthread_mutex_unlock");
}

/* expect val: {-1,0,1,...,(changes - 1)}
 * count will therefore run 0...changes.
 */
static int mt_watch_cb (const char *k, int val, void *arg, int errnum)
{
    thd_t *t = arg;
    if (errnum != 0) {
        errn (errnum, "%d: %s", t->n, __FUNCTION__);
        return -1;
    }
    if (val == t->last_val) {
        msg ("%d: %s: called with same value as last time: %d", t->n,
            __FUNCTION__, val);
        return -1;
    }
    t->last_val = val;

    /* normal stop */
    if (val + 1 == changes)
        flux_reactor_stop (t->h);
    t->change_count++;
    return 0;
}

/* Watch a key that does not exist throughout the test.
 */
static int mt_watchnil_cb (const char *k, int val, void *arg, int errnum)
{
    thd_t *t = arg;
    if (errnum != ENOENT) {
        errn (errnum, "%d: %s", t->n, __FUNCTION__);
        return -1;
    }
    t->nil_count++;
    return 0;
}

/* Watch a key that exists but does not change throughout the test.
 */
static int mt_watchstable_cb (const char *k, int val, void *arg, int errnum)
{
    thd_t *t = arg;

    if (errnum != 0) {
        errn (errnum, "%d: %s", t->n, __FUNCTION__);
        return -1;
    }
    t->stable_count++;
    return 0;
}

void *thread (void *arg)
{
    thd_t *t = arg;

    if (!(t->h = flux_open (NULL, 0))) {
        err ("%d: flux_open", t->n);
        goto done;
    }
    signal_ready ();
    /* The first kvs.watch reply is handled synchronously, then other kvs.watch
     * replies will arrive asynchronously and be handled by the reactor.
     */
    if (kvs_watch_int (t->h, key, mt_watch_cb, t) < 0) {
        err ("%d: kvs_watch_int", t->n);
        goto done;
    }
    if (kvs_watch_int (t->h, "nonexistent-key", mt_watchnil_cb, t) < 0) {
        err ("%d: kvs_watch_int", t->n);
        goto done;
    }
    if (kvs_watch_int (t->h, key_stable, mt_watchstable_cb, t) < 0) {
        err ("%d: kvs_watch_int", t->n);
        goto done;
    }
    if (flux_reactor_start (t->h) < 0) {
        err ("%d: flux_reactor_start", t->n);
        goto done;
    }
done:
    if (t->h)
        flux_close (t->h);

    return NULL;
}

void usage (void)
{
    fprintf (stderr,
"Usage: tkvswatch  mt         nthreads changes key\n"
"                  selfmod    key\n"
"                  unwatch    key\n"
"                  unwatchloop key\n"
);
    exit (1);

}

void test_mt (int argc, char **argv)
{
    thd_t *thd;
    int i, rc;
    flux_t h;
    int errors = 0;

    if (argc != 3) {
        fprintf (stderr, "Usage: mt nthreads changes key\n");
        exit (1);
    }
    nthreads = strtoul (argv[0], NULL, 10);
    changes = strtoul (argv[1], NULL, 10);
    key = argv[2];

    thd = xzmalloc (sizeof (*thd) * nthreads);

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    /* Set initial value of 'key' to -1 */
    if (kvs_put_int (h, key, -1) < 0)
        err_exit ("kvs_put_int %s", key);
    key_stable = xasprintf ("%s-stable", key);
    if (kvs_put_int (h, key_stable, 0) < 0)
        err_exit ("kvs_put_int %s", key);

    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        thd[i].last_val = -42;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].tid, &thd[i].attr, thread, &thd[i])))
            errn (rc, "pthread_create");
    }
    wait_ready ();

    for (i = 0; i < changes; i++) {
        if (kvs_put_int (h, key, i) < 0)
            err_exit ("kvs_put_int %s", key);
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");
    }

    /* Verify that callbacks were called the correct number of times.
     * The nil and stable callbacks will be called exactly once before the
     * reactor is started, then should never be called again.
     * Due to commit merging on the master, the changing callback may
     * miss intervening values but it shouldn't be called extra times.
     */
    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].tid, NULL)))
            errn (rc, "pthread_join");
        if (thd[i].nil_count != 1) {
            msg ("%d: nil callback called %d times (expected one)",
                 i, thd[i].nil_count);
            errors++;
        }
        if (thd[i].stable_count != 1) {
            msg ("%d: stable callback called %d times (expected one)",
                 i, thd[i].stable_count);
            errors++;
        }
        if (thd[i].change_count > changes + 1) {
            msg ("%d: changing callback called %d times (expected <= %d)",
                 i, thd[i].change_count, changes + 1);
            errors++;
        }
    }
    if (errors > 0)
        exit (1);

    free (thd);
    free (key_stable);

    flux_close (h);
}

static int selfmod_watch_cb (const char *key, int val, void *arg, int errnum)
{
    msg ("%s: value = %d errnum = %d", __FUNCTION__, val, errnum);

    flux_t h = arg;
    if (kvs_put_int (h, key, val + 1) < 0)
        err_exit ("%s: kvs_put_int", __FUNCTION__);
    if (kvs_commit (h) < 0)
        err_exit ("%s: kvs_commit", __FUNCTION__);
    return (val == 0 ? -1 : 0);
}

void test_selfmod (int argc, char **argv)
{
    flux_t h;
    char *key;

    if (argc != 1) {
        fprintf (stderr, "Usage: selfmod key\n");
        exit (1);
    }
    key = argv[0];
    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    if (kvs_put_int (h, key, -1) < 0)
        err_exit ("kvs_put_int");
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (kvs_watch_int (h, key, selfmod_watch_cb, h) < 0)
        err_exit ("kvs_watch_int");

    msg ("reactor: start");
    flux_reactor_start (h);
    msg ("reactor: end");

    flux_close (h);
}

static int unwatch_watch_cb (const char *key, int val, void *arg, int errnum)
{
    int *count = arg;
    (*count)++;
    return 0;
}

static int unwatch_timer_cb (flux_t h, void *arg)
{
    static int count = 0;
    const char *key = arg;
    if (kvs_put_int (h, key, count++) < 0)
        err_exit ("%s: kvs_put_int", __FUNCTION__);
    if (kvs_commit (h) < 0)
        err_exit ("%s: kvs_commit", __FUNCTION__);
    if (count == 10) {
        if (kvs_unwatch (h, key) < 0)
            err_exit ("%s: kvs_unwatch", __FUNCTION__);
    } else if (count == 20)
        flux_reactor_stop (h);
    return 0;
}

/* Timer pops every 1 ms, writing a new value to key.
 * After 10 calls, it calls kvs_unwatch().
 * After 20 calls, it calls flux_reactor_stop().
 * The kvs_unwatch_cb() counts the number of times it is called, should be 10.
 */
void test_unwatch (int argc, char **argv)
{
    flux_t h;
    char *key;
    int count = 0;

    if (argc != 1) {
        fprintf (stderr, "Usage: unwatch key\n");
        exit (1);
    }
    key = argv[0];
    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (kvs_watch_int (h, key, unwatch_watch_cb, &count) < 0)
        err_exit ("kvs_watch_int %s", key);
    if (flux_tmouthandler_add (h, 1, false, unwatch_timer_cb, key) < 0)
        err_exit ("flux_tmouthandler_add");
    if (flux_reactor_start (h) < 0)
        err_exit ("flux_reactor_start");
    if (count != 10)
        msg_exit ("watch called %d times (should be 10)", count);
    flux_close (h);
}

static int unwatchloop_cb (const char *key, int val, void *arg, int errnum)
{
    return 0;
}

/* This is a sanity check that watch/unwatch in a loop doesn't
 * leak matchtags.
 */
void test_unwatchloop (int argc, char **argv)
{
    int i;
    flux_t h;
    char *key;

    if (argc != 1) {
        fprintf (stderr, "Usage: unwatch key\n");
        exit (1);
    }
    key = argv[0];
    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    uint32_t avail = flux_matchtag_avail (h);
    for (i = 0; i < 1000; i++) {
        if (kvs_watch_int (h, key, unwatchloop_cb, NULL) < 0)
            err_exit ("kvs_watch_int[%d] %s", i, key);
        if (kvs_unwatch (h, key) < 0)
            err_exit ("kvs_unwatch[%d] %s", i, key);
    }
    uint32_t leaked = avail - flux_matchtag_avail (h);
    if (leaked > 0)
        msg_exit ("leaked %u matchtags", leaked);

    flux_close (h);
}

int main (int argc, char *argv[])
{
    char *cmd;

    if (argc == 1)
        usage ();
    cmd = argv[1];

    log_init (basename (argv[0]));

    if (!strcmp (cmd, "mt"))
        test_mt (argc - 2, argv + 2);
    else if (!strcmp (cmd, "selfmod"))
        test_selfmod (argc - 2, argv + 2);
    else if (!strcmp (cmd, "unwatch"))
        test_unwatch (argc - 2, argv + 2);
    else if (!strcmp (cmd, "unwatchloop"))
        test_unwatchloop (argc - 2, argv + 2);
    else
        usage ();

    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
