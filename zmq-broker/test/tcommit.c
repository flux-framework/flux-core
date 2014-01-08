/* tcommit - performance test for KVS commits */

#define _GNU_SOURCE
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <pthread.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

typedef struct {
    pthread_t t;
    pthread_attr_t attr;
    int n;
    flux_t h;
} thd_t;

static int count = -1;
static int nthreads = -1;
static char *prefix = NULL;

void *thread (void *arg)
{
    thd_t *t = arg;
    char *key;
    int i;

    if (!(t->h = cmb_init ())) {
        err ("%d: cmb_init", t->n);
        goto done;
    }
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.%d.%d", prefix, t->n, i) < 0)
            oom ();
        if (kvs_put_int (t->h, key, 42) < 0)
            err_exit ("%s", key);
        if (kvs_commit (t->h) < 0)
            err_exit ("kvs_commit");
        free (key);
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

    log_init (basename (argv[0]));

    if (argc != 4) {
        fprintf (stderr, "Usage: tcommit nthreads count prefix\n");
        exit (1);
    }
    nthreads = strtoul (argv[1], NULL, 10);
    count = strtoul (argv[2], NULL, 10);
    prefix = argv[3];

    thd = xzmalloc (sizeof (*thd) * nthreads);

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, thread, &thd[i])))
            errn (rc, "pthread_create");
    }

    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            errn (rc, "pthread_join");
    }

    free (thd);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
