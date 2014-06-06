/* flux-insmod.c - insert module subcommand */

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

static char *modfind (const char *modpath, const char *name);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-insmod [--rank N] module [arg=val ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    char *path;
    flux_t h;
    int ch;
    int i;
    json_object *args;
    int rank = -1;
    char *trypath = NULL;

    log_init ("flux-insmod");

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
    path = argv[optind++];
    if (access (path, R_OK|X_OK) < 0) {
        if (!(trypath = modfind (PLUGIN_PATH, path)))
            errn_exit (ENOENT, "%s", path);
        path = trypath;
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
    if (flux_insmod (h, rank, path, args) < 0)
        err_exit ("%s", path);
    json_object_put (args);
    msg ("module loaded");

    flux_handle_destroy (&h);
    log_fini ();
    if (trypath)
        free (trypath);
    return 0;
}


static char *modfind (const char *modpath, const char *name)
{
    char *cpy = xstrdup (modpath);
    char *path = NULL, *dir, *saveptr, *a1 = cpy;
    char *ret = NULL;

    while (!ret && (dir = strtok_r (a1, ":", &saveptr))) {
        if (asprintf (&path, "%s/%s.so", dir, name) < 0)
            oom ();
        if (access (path, R_OK|X_OK) < 0)
            free (path);
        else
            ret = path;
        a1 = NULL;
    }
    free (cpy);
    if (!ret)
        errno = ENOENT;
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
