/* tkvswatch.c - exercise kvs watch functions */



/* Usage ./tkvswatch nthreads changes key
 * Spawn 'nthreads' threads each watching the same value.
 * Change it 'changes' times and ensure that all the changes are
 * delivered to all the threads.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <czmq.h>
#include <libgen.h>
#include <pthread.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "zmsg.h"

#define USE_REACTOR 1

int changes = -1;
int nthreads = -1;
char *key = NULL;

static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;
static int start_count = 0;


typedef struct {
    pthread_t tid;
    pthread_attr_t attr;
    int n;
    int count;
    flux_t h;
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
static void watch_cb (const char *k, int val, void *arg, int errnum)
{
    thd_t *t = arg;

    t->count++;
    if (t->count == changes)
        flux_reactor_stop (t->h);
}

void *thread (void *arg)
{
    thd_t *t = arg;

    if (!(t->h = cmb_init ())) {
        err ("%d: cmb_init", t->n);
        goto done;
    }
    signal_ready ();
    /* The first kvs.watch reply is handled synchronously, then other kvs.watch
     * replies will arrive asynchronously and be handled by the reactor.
     */
    if (kvs_watch_int (t->h, key, watch_cb, t) < 0) {
        err ("%d: kvs_watch_int", t->n);
        goto done;
    }
    if (flux_reactor_start (t->h) < 0) {
        err ("%d: flux_reactor_start", t->n);
        goto done;
    }
done:
    if (t->h)
        flux_handle_destroy (&t->h);

    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    int i, rc;
    flux_t h;

    log_init (basename (argv[0]));

    if (argc != 4) {
        fprintf (stderr, "Usage: tkvswatch nthreads changes key\n");
        exit (1);
    }
    nthreads = strtoul (argv[1], NULL, 10);
    changes = strtoul (argv[2], NULL, 10);
    key = argv[3];

    thd = xzmalloc (sizeof (*thd) * nthreads);
    
    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (kvs_put_int (h, key, -1) < 0)
        err_exit ("kvs_put_int %s", key);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        thd[i].count = 0;
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
    
    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].tid, NULL)))
            errn (rc, "pthread_join");
        assert (thd[i].count == changes);
    }

    free (thd);

    flux_handle_destroy (&h);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
