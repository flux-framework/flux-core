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

/* watch.c - exercise kvs watch functions */

/* Usage watch nthreads changes key
 * Spawn 'nthreads' threads each watching the same value.
 * Change it 'changes' times and ensure that at minimum the last
 * value is read.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <pthread.h>

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
    flux_t *h;
    int change_count;
    int nil_count;
    int stable_count;
    int last_val;
    int errcount;
} thd_t;

static void signal_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        log_errn_exit (rc, "pthread_mutex_lock");
    start_count++;
    if ((rc = pthread_mutex_unlock (&start_lock)))
        log_errn_exit (rc, "pthread_mutex_unlock");
    if ((rc = pthread_cond_signal (&start_cond)))
        log_errn_exit (rc, "pthread_cond_signal");
}

static void wait_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        log_errn_exit (rc, "pthread_mutex_lock");
    while (start_count < nthreads) {
        if ((rc = pthread_cond_wait (&start_cond, &start_lock)))
            log_errn_exit (rc, "pthread_cond_wait");
    }
    if ((rc = pthread_mutex_unlock (&start_lock)))
        log_errn_exit (rc, "pthread_mutex_unlock");
}

/* expect val: {-1,0,1,...,(changes - 1)}
 * count will therefore run 0...changes.
 */
static int mt_watch_cb (const char *k, const char *json_str, void *arg, int errnum)
{
    thd_t *t = arg;
    json_t *obj;
    int val;

    if (errnum != 0) {
        log_errn (errnum, "%d: %s", t->n, __FUNCTION__);
        t->errcount++;
        return -1;
    }
    if (!(obj = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
        log_msg ("%d: %s failed to decode value", t->n, __FUNCTION__);
        t->errcount++;
        return -1;
    }
    val = json_integer_value (obj);
    if (val == t->last_val) {
        log_msg ("%d: %s: called with same value as last time: %d", t->n,
            __FUNCTION__, val);
        t->errcount++;
        return -1;
    }
    t->last_val = val;

    /* normal stop */
    if (val + 1 == changes)
        flux_reactor_stop (flux_get_reactor (t->h));
    t->change_count++;
    json_decref (obj);
    return 0;
}

/* Watch a key that does not exist throughout the test.
 */
static int mt_watchnil_cb (const char *k, const char *json_str, void *arg, int errnum)
{
    thd_t *t = arg;
    if (errnum != ENOENT) {
        log_errn (errnum, "%d: %s", t->n, __FUNCTION__);
        t->errcount++;
        return -1;
    }
    t->nil_count++;
    return 0;
}

/* Watch a key that exists but does not change throughout the test.
 */
static int mt_watchstable_cb (const char *k, const char *json_ttr, void *arg, int errnum)
{
    thd_t *t = arg;

    if (errnum != 0) {
        log_errn (errnum, "%d: %s", t->n, __FUNCTION__);
        t->errcount++;
        return -1;
    }
    t->stable_count++;
    return 0;
}

void *thread (void *arg)
{
    thd_t *t = arg;

    if (!(t->h = flux_open (NULL, 0))) {
        log_err ("%d: flux_open", t->n);
        goto done;
    }
    signal_ready ();
    /* The first kvs.watch reply is handled synchronously, then other kvs.watch
     * replies will arrive asynchronously and be handled by the reactor.
     */
    if (flux_kvs_watch (t->h, key, mt_watch_cb, t) < 0) {
        log_err ("%d: flux_kvs_watch", t->n);
        goto done;
    }
    if (flux_kvs_watch (t->h, "nonexistent-key", mt_watchnil_cb, t) < 0) {
        log_err ("%d: flux_kvs_watch", t->n);
        goto done;
    }
    if (flux_kvs_watch (t->h, key_stable, mt_watchstable_cb, t) < 0) {
        log_err ("%d: flux_kvs_watch", t->n);
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (t->h), 0) < 0) {
        log_err ("%d: flux_reactor_run", t->n);
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
"Usage: watch  mt         nthreads changes key\n"
"              selfmod    key\n"
"              unwatch    key\n"
"              unwatchloop key\n"
"              simulwatch key ntimes\n"
);
    exit (1);

}

void test_mt (int argc, char **argv)
{
    thd_t *thd;
    int i, rc;
    flux_t *h;
    int errors = 0;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (argc != 3) {
        fprintf (stderr, "Usage: mt nthreads changes key\n");
        exit (1);
    }
    nthreads = strtoul (argv[0], NULL, 10);
    changes = strtoul (argv[1], NULL, 10);
    key = argv[2];

    thd = xzmalloc (sizeof (*thd) * nthreads);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* Set initial value of 'key' to -1 */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, key, "i", -1) < 0)
        log_err_exit ("flux_kvs_txn_pack %s", key);
    key_stable = xasprintf ("%s-stable", key);
    if (flux_kvs_txn_pack (txn, 0, key_stable, "i", 0) < 0)
        log_err_exit ("flux_kvs_txn_pack %s", key);
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        thd[i].last_val = -42;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].tid, &thd[i].attr, thread, &thd[i])))
            log_errn (rc, "pthread_create");
    }
    wait_ready ();

    for (i = 0; i < changes; i++) {
        if (!(txn = flux_kvs_txn_create ()))
            log_err_exit ("flux_kvs_txn_create");
        if (flux_kvs_txn_pack (txn, 0, key, "i", i) < 0)
            log_err_exit ("flux_kvs_txn_pack %s", key);
        if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
            log_err_exit ("flux_kvs_commit");
        flux_future_destroy (f);
        flux_kvs_txn_destroy (txn);
    }

    /* Verify that callbacks were called the correct number of times.
     * The nil and stable callbacks will be called exactly once before the
     * reactor is started, then should never be called again.
     * Due to commit merging on the master, the changing callback may
     * miss intervening values but it shouldn't be called extra times.
     */
    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].tid, NULL)))
            log_errn (rc, "pthread_join");
        if (thd[i].errcount != 0) {
            log_msg ("%d: error occurred inside callback function", i);
            errors++;
        }
        if (thd[i].nil_count != 1) {
            log_msg ("%d: nil callback called %d times (expected one)",
                 i, thd[i].nil_count);
            errors++;
        }
        if (thd[i].stable_count != 1) {
            log_msg ("%d: stable callback called %d times (expected one)",
                 i, thd[i].stable_count);
            errors++;
        }
        if (thd[i].change_count > changes + 1) {
            log_msg ("%d: changing callback called %d times (expected <= %d)",
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

static int selfmod_watch_cb (const char *key, const char *json_str, void *arg, int errnum)
{
    int val;
    json_t *obj;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    log_msg ("%s: value = %s errnum = %d", __FUNCTION__, json_str, errnum);

    if (!(obj = json_loads (json_str, JSON_DECODE_ANY, NULL)))
        log_msg_exit ("%s: failed to decode json value", __FUNCTION__);
    val = json_integer_value (obj);
    flux_t *h = arg;
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, key, "i", val + 1) < 0)
        log_err_exit ("%s: flux_kvs_txn_pack", __FUNCTION__);
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("%s: flux_kvs_commit", __FUNCTION__);
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    json_decref (obj);
    return (val == 0 ? -1 : 0);
}

void test_selfmod (int argc, char **argv)
{
    flux_t *h;
    char *key;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (argc != 1) {
        fprintf (stderr, "Usage: selfmod key\n");
        exit (1);
    }
    key = argv[0];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, key, "i", -1) < 0)
        log_err_exit ("flux_kvs_txn_pack");
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    if (flux_kvs_watch (h, key, selfmod_watch_cb, h) < 0)
        log_err_exit ("flux_kvs_watch");

    log_msg ("reactor: start");
    flux_reactor_run (flux_get_reactor (h), 0);
    log_msg ("reactor: end");

    flux_close (h);
}

static int unwatch_watch_cb (const char *key, const char *json_str, void *arg, int errnum)
{
    int *count = arg;
    (*count)++;
    return 0;
}

struct timer_ctx {
    flux_t *h;
    char *key;
};

static void unwatch_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    struct timer_ctx *ctx = arg;
    static int count = 0;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    log_msg ("%s", __FUNCTION__);

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, ctx->key, "i", count++) < 0)
        log_err_exit ("%s: flux_kvs_txn_pack", __FUNCTION__);
    if (!(f = flux_kvs_commit (ctx->h, 0, txn))
                || flux_future_get (f, NULL) < 0)
        log_err_exit ("%s: flux_kvs_commit", __FUNCTION__);
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    if (count == 10) {
        if (flux_kvs_unwatch (ctx->h, ctx->key) < 0)
            log_err_exit ("%s: flux_kvs_unwatch", __FUNCTION__);
    } else if (count == 20)
        flux_reactor_stop (r);
}

/* Timer pops every 1 ms, writing a new value to key.
 * After 10 calls, it calls flux_kvs_unwatch().
 * After 20 calls, it calls flux_reactor_stop().
 * The kvs_unwatch_cb() counts the number of times it is called, should be 10.
 */
void test_unwatch (int argc, char **argv)
{
    struct timer_ctx ctx;
    flux_reactor_t *r;
    int count = 0;
    flux_watcher_t *timer;

    if (argc != 1) {
        fprintf (stderr, "Usage: unwatch key\n");
        exit (1);
    }
    ctx.key = argv[0];
    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    r = flux_get_reactor (ctx.h);
    if (flux_kvs_watch (ctx.h, ctx.key, unwatch_watch_cb, &count) < 0)
        log_err_exit ("flux_kvs_watch %s", ctx.key);
    if (!(timer = flux_timer_watcher_create (r, 0.001, 0.001,
                                             unwatch_timer_cb, &ctx)))
        log_err_exit ("flux_timer_watcher_create");
    flux_watcher_start (timer);
    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");
    if (count != 10)
        log_msg_exit ("watch called %d times (should be 10)", count);
    flux_watcher_destroy (timer);
    flux_close (ctx.h);
}

static int unwatchloop_cb (const char *key, const char *json_str, void *arg, int errnum)
{
    return 0;
}

/* This is a sanity check that watch/unwatch in a loop doesn't
 * leak matchtags.
 */
void test_unwatchloop (int argc, char **argv)
{
    int i;
    flux_t *h;
    char *key;

    if (argc != 1) {
        fprintf (stderr, "Usage: unwatch key\n");
        exit (1);
    }
    key = argv[0];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    uint32_t avail = flux_matchtag_avail (h, 0);
    for (i = 0; i < 1000; i++) {
        if (flux_kvs_watch (h, key, unwatchloop_cb, NULL) < 0)
            log_err_exit ("flux_kvs_watch[%d] %s", i, key);
        if (flux_kvs_unwatch (h, key) < 0)
            log_err_exit ("flux_kvs_unwatch[%d] %s", i, key);
    }
    uint32_t leaked = avail - flux_matchtag_avail (h, 0);
    if (leaked > 0)
        log_msg_exit ("leaked %u matchtags", leaked);

    flux_close (h);
}

static int simulwatch_cb (const char *key, const char *json_str, void *arg, int errnum)
{
    int *count = arg;
    (*count)++;
    return 0;
}

int get_watch_stats (flux_t *h, int *count)
{
    flux_future_t *f;
    int rc = -1;

    if (!(f = flux_rpc (h, "kvs.stats.get", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get_unpack (f, "{ s:i }", "#watchers", count) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

void test_simulwatch (int argc, char **argv)
{
    int i, max;
    const char *key;
    flux_t *h;
    int start, fin, count = 0;
    int exit_rc = 0;

    if (argc != 2) {
        fprintf (stderr, "Usage: simulwatch key count\n");
        exit (1);
    }
    key = argv[0];
    max = strtoul (argv[1], NULL, 10);
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (get_watch_stats (h, &start) < 0)
        log_err_exit ("kvs.stats.get");
    for (i = 0; i < max; i++) {
        if (flux_kvs_watch (h, key, simulwatch_cb, &count) < 0)
            log_err_exit ("flux_kvs_watch[%d] %s", i, key);
        if ((i % 4096 == 0 && i > 0 && i + 4096 < max))
            log_msg ("%d of %d watchers registered (continuing)", i, max);
    }
    log_msg ("%d of %d watchers registered", i, max);
    if (count != max)
        exit_rc = 1;
    log_msg ("callback called %d of %d times", count, max);
    if (get_watch_stats (h, &fin) < 0)
        log_err_exit ("kvs.stats.get");
    if (fin - start != count)
        exit_rc = 1;
    log_msg ("%d of %d watchers running", fin - start, count);
    if (flux_kvs_unwatch (h, key) < 0)
        log_err_exit ("kvs.unwatch");
    if (get_watch_stats (h, &fin) < 0)
        log_err_exit ("kvs.stats.get");
    if (fin - start != 0)
        exit_rc = 1;
    log_msg ("%d of %d watchers running after unwatch",
             fin - start, count);
    flux_close (h);
    if (exit_rc != 0)
        exit (exit_rc);
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
    else if (!strcmp (cmd, "simulwatch"))
        test_simulwatch (argc - 2, argv + 2);
    else
        usage ();

    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
