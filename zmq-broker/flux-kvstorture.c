/* flux-kvstorture.c - flux kvstorture subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "ht:"
static const struct option longopts[] = {
    {"help",            no_argument,  0, 'h'},
    {"test-iterations", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-kvstorture [--test-iterations N]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int i, iter = -1;
    char key[64], val[64];
    struct timespec t0;
    json_object *vo = NULL;

    log_init ("flux-kvstorture");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 't': /* --test-iterations */
                iter = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc || iter == -1)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (kvs_unlink (h, "kvstorture") < 0)
        err_exit ("kvs_unlink");

    monotime (&t0);
    for (i = 0; i < iter; i++) {
        snprintf (key, sizeof (key), "kvstorture.key%d", i);
        snprintf (val, sizeof (key), "kvstorture.val%d", i);
        vo = json_object_new_string (val);
        if (kvs_put (h, key, vo) < 0)
            err_exit ("kvs_put");
        if (vo)
            json_object_put (vo);
    }
    msg ("kvs_put:    time=%0.3f ms (%d iterations)",
         monotime_since (t0), iter);

    monotime (&t0);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    msg ("kvs_commit: time=%0.3f ms", monotime_since (t0));

    monotime (&t0);
    for (i = 0; i < iter; i++) {
        snprintf (key, sizeof (key), "kvstorture.key%d", i);
        snprintf (val, sizeof (key), "kvstorture.val%d", i);
        if (kvs_get (h, key, &vo) < 0)
            err_exit ("kvs_get");
        if (strcmp (json_object_get_string (vo), val) != 0)
            msg_exit ("kvs_get: key '%s' wrong value '%s'",
                      key, json_object_get_string (vo));
        if (vo)
            json_object_put (vo);
    }
    msg ("kvs_get:    time=%0.3f ms (%d iterations)",
         monotime_since (t0), iter);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
