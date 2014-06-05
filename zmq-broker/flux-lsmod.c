/* flux-lsmod.c - list modules subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hlr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"long",       no_argument,        0, 'l'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

static void list_module (bool lopt, const char *key, json_object *mo);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-lsmod [--rank N] [--long]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    json_object *mods;
    json_object_iter iter;
    bool lopt = false;
    int rank = -1;

    log_init ("flux-rmmod");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'l': /* --long */
                lopt = true;
                break;
            case 'r': /* --rank */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (!(mods = flux_lsmod (h, rank)))
        err_exit ("flux_lsmod");

    printf ("%-20s %6s %s\n", "Module", "Size", "Digest");
    json_object_object_foreachC (mods, iter) {
        list_module (lopt, iter.key, iter.val);
    }
    json_object_put (mods);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void list_module (bool lopt, const char *key, json_object *mo)
{
    const char *name, *digest;
    int size;
    json_object *args;
    json_object_iter iter;

    if (util_json_object_get_string (mo, "name", &name) < 0
            || util_json_object_get_string (mo, "digest", &digest) < 0
            || util_json_object_get_int (mo, "size", &size) < 0
            || !(args = json_object_object_get (mo, "args")))
        msg_exit ("error parsing lsmod response");

    printf ("%-20.20s %6d %8.8s  ", key, size, digest);
    if (lopt) {
        printf ("%-10.10s ", name);
        json_object_object_foreachC (args, iter) {
            const char *val = json_object_get_string (iter.val);
            if (val)
                printf ("%s=%s ", iter.key, val);
            else
                printf ("%s ", iter.key);
        }
    }
    printf ("\n");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
