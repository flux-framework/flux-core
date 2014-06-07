/* flux-mod.c - module subcommand */

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

static void mod_ls (flux_t h, int rank, int argc, char **argv);
static void mod_rm (flux_t h, int rank, int argc, char **argv);
static void mod_ins (flux_t h, int rank, int argc, char **argv);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-mod [--rank N] ls\n"
"       flux-mod [--rank N] rm module [module...]\n"
"       flux-mod [--rank N] ins module [arg=val ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int rank = -1;
    char *cmd;

    log_init ("flux-rmmod");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (!strcmp (cmd, "ls"))
        mod_ls (h, rank, argc - optind, argv + optind);
    else if (!strcmp (cmd, "rm"))
        mod_rm (h, rank, argc - optind, argv + optind);
    else if (!strcmp (cmd, "ins"))
        mod_ins (h, rank, argc - optind, argv + optind);
    else
        usage ();

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void list_module (const char *key, json_object *mo)
{
    const char *name, *digest;
    int size;

    if (util_json_object_get_string (mo, "name", &name) < 0
            || util_json_object_get_string (mo, "digest", &digest) < 0
            || util_json_object_get_int (mo, "size", &size) < 0)
        msg_exit ("error parsing lsmod response");

    printf ("%-20.20s %6d %8.8s\n", key, size, digest);
}

static void mod_ls (flux_t h, int rank, int argc, char **argv)
{
    json_object *mods;
    json_object_iter iter;

    if (argc != 0)
        usage ();

    if (!(mods = flux_lsmod (h, rank)))
        err_exit ("flux_lsmod");
    printf ("%-20s %6s %s\n", "Module", "Size", "Digest");
    json_object_object_foreachC (mods, iter) {
        list_module (iter.key, iter.val);
    }
    json_object_put (mods);
}

static void mod_rm (flux_t h, int rank, int argc, char **argv)
{
    int i;

    if (argc == 0)
        usage ();
    for (i = 0; i < argc; i++) {
        if (flux_rmmod (h, rank, argv[i]) < 0)
            err ("%s", argv[i]);
        else
            msg ("%s: unloaded", argv[i]);
    }
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

static void mod_ins (flux_t h, int rank, int argc, char **argv)
{
    json_object *args = util_json_object_new_object ();
    int i;
    char *path, *trypath = NULL;

    if (argc == 0)
        usage ();
    path = argv[0];
    if (access (path, R_OK|X_OK) < 0) {
        if (!(trypath = modfind (PLUGIN_PATH, path)))
            errn_exit (ENOENT, "%s", path);
        path = trypath;
    }
    for (i = 1; i < argc; i++) {
        char *val, *cpy = xstrdup (argv[i]);
        if ((val == strchr (cpy, '=')))
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
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
