/* tcommit - performance test for KVS commits */

#define _GNU_SOURCE
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

int main (int argc, char *argv[])
{
    flux_t h;
    int i, j, k;
    char *key;
    struct timespec t0;

    log_init ("tcommit");

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    /* 262144 entries, fanout 64 */
    for (i = 0; i < 64; i++) {
        for (j = 0; j < 64; j++) {
            for (k = 0; k < 64; k++) {
                monotime (&t0);
                if (asprintf (&key, "%d.%d.%d", i, j, k) < 0)
                    oom ();
                if (kvs_put_int (h, key, 42) < 0)
                    err_exit ("kvs_put_int %s", key);
                if (kvs_commit (h) < 0)
                    err_exit ("kvs_commit");
                free (key);
                printf ("%d\t%lf\n", i*64*64 + j*64 + k, monotime_since (t0));
            }
        }
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
