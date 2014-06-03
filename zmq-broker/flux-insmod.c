/* flux-insmod.c - flux insmod subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-insmod modulename [arg=val ...]\n"
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

    log_init ("flux-insmod");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
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
        util_json_object_add_string (args, cpy, val);
        free (cpy);
    }
    if (flux_insmod (h, name, args) < 0)
        err_exit ("flux_insmod");
    json_object_put (args);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
