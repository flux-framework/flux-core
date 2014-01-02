/* flux-kvstorture.c - flux kvstorture subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hc:s:p:qv"
static const struct option longopts[] = {
    {"help",            no_argument,        0, 'h'},
    {"quiet",           no_argument,        0, 'q'},
    {"verbose",         no_argument,        0, 'v'},
    {"count",           required_argument,  0, 'c'},
    {"size",            required_argument,  0, 's'},
    {"prefix",          required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

static void fill (char *s, int i, int len);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-kvstorture [--quiet|--verbose] [--prefix NAME] [--size BYTES] [--count N]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int i, count = 20;
    int size = 20;
    char *key, *val;
    bool quiet = false;
    struct timespec t0;
    json_object *vo = NULL;
    char *prefix = "kvstorture";
    bool verbose = false;
    const char *s;

    log_init ("flux-kvstorture");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 's': /* --size BYTES */
                size = strtoul (optarg, NULL, 10);
                break;
            case 'c': /* --count */
                count = strtoul (optarg, NULL, 10);
                break;
            case 'p': /* --prefix NAME */
                prefix = optarg;
                break;
            case 'v': /* --verbose */
                verbose = true;
                break;
            case 'q': /* --quiet */
                quiet = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();
    if (size < 1 || count < 1)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (kvs_unlink (h, prefix) < 0)
        err_exit ("kvs_unlink %s", prefix);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    
    val = xzmalloc (size);
    
    monotime (&t0);
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        vo = json_object_new_string (val);
        if (kvs_put (h, key, vo) < 0)
            err_exit ("kvs_put %s", key);
        if (verbose)
            msg ("%s = %s", key, val);
        if (vo)
            json_object_put (vo);
        free (key);
    }
    if (!quiet)
        msg ("kvs_put:    time=%0.3f s (%d keys of size %d)",
             monotime_since (t0)/1000, count, size);

    monotime (&t0);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (!quiet)
        msg ("kvs_commit: time=%0.3f s", monotime_since (t0)/1000);

    monotime (&t0);
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        if (kvs_get (h, key, &vo) < 0)
            err_exit ("kvs_get '%s'", key);
        s = json_object_get_string (vo);
        if (verbose)
            msg ("%s = %s", key, s);
        if (strcmp (s, val) != 0)
            msg_exit ("kvs_get: key '%s' wrong value '%s'",
                      key, json_object_get_string (vo));
        if (vo)
            json_object_put (vo);
        free (key);
    }
    if (!quiet)
        msg ("kvs_get:    time=%0.3f s (%d keys of size %d)",
             monotime_since (t0)/1000, count, size);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void fill (char *s, int i, int len)
{
    snprintf (s, len, "%d", i);
    memset (s + strlen (s), 'x', len - strlen (s) - 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
