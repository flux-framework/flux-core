/* tcommit - performance test for KVS commits */

#define _GNU_SOURCE
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>

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
static bool fopt = false;

#define OPTIONS "f"
static const struct option longopts[] = {
   {"fence",   no_argument,         0, 'f'},
   {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: tcommit [--fence] nthreads count prefix\n");
    exit (1);
}

void *thread (void *arg)
{
    thd_t *t = arg;
    char *key, *fence = NULL;
    int i;

    if (!(t->h = cmb_init ())) {
        err ("%d: cmb_init", t->n);
        goto done;
    }
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.%d.%d", prefix, t->n, i) < 0)
            oom ();
        if (fopt && asprintf (&fence, "%s-%d", prefix, i) < 0)
            oom ();
        if (kvs_put_int (t->h, key, 42) < 0)
            err_exit ("%s", key);
        if (fopt) {
            if (kvs_fence (t->h, fence, nthreads) < 0)
                err_exit ("kvs_commit");
        } else {
            if (kvs_commit (t->h) < 0)
                err_exit ("kvs_commit");
        }
        free (key);
        if (fence)
            free (fence);
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
    int ch;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'f':
                fopt = true;
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 3)
        usage ();

    nthreads = strtoul (argv[optind++], NULL, 10);
    count = strtoul (argv[optind++], NULL, 10);
    prefix = argv[optind++];

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
