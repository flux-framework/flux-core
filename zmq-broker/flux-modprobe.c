/* flux-modprobe.c - insert module (by name) subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-modprobe [--rank N] modulename [arg=val ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int i;
    char *name;
    json_object *args;
    int rank = -1;

    log_init ("flux-modprobe");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --help */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    name = argv[optind++];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");
   
    args = util_json_object_new_object (); 
    for (i = optind; i < argc; i++) {
        char *val, *cpy = xstrdup (argv[i]);
        if ((val = strchr (cpy, '=')))
            *val++ = '\0';
        if (!val)
            msg_exit ("malformed argument: %s", cpy);
        util_json_object_add_string (args, cpy, val);
        free (cpy);
    }
    if (flux_insmod (h, rank, NULL, name, args) < 0)
        err_exit ("%s", name);
    json_object_put (args);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
