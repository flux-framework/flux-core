/* flux-insmod.c - insert module subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hr:n:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"name",       required_argument,  0, 'n'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-insmod [--rank N] [--name NAME] module [arg=val ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    char *name = NULL, *path, *p, *cpy = NULL;
    flux_t h;
    int ch;
    int i;
    json_object *args;
    int rank = -1;

    log_init ("flux-insmod");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --help */
                rank = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --name NAME */
                name = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    path = argv[optind++];

    /* If no name specified, guess it from the module path.
     */
    if (!name) {
        cpy = xstrdup (path);
        name = basename (cpy);
        if ((p = strstr (name, ".so")))
            *p = '\0';
    }

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
    if (flux_insmod (h, rank, path, name, args) < 0) {
        if (errno == ENOENT)
            err_exit ("%s", path);
        else if (errno == EEXIST)
            err_exit ("%s", name);
        else
            err_exit ("%s: %s", name, path);
    }
    msg ("%s: loaded", name);
    json_object_put (args);

    flux_handle_destroy (&h);
    log_fini ();
    if (cpy)
        free (cpy);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
