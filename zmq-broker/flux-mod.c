/* flux-mod.c - module subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <dlfcn.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "shortjson.h"

#define OPTIONS "+hr:"
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
"       flux-mod [--rank N] ins [-m] module [arg=val ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int rank = -1;
    char *cmd;

    log_init ("flux-mod");

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
    cmd = argv[optind];

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

static void list_module (const char *key, JSON mo)
{
    const char *name;
    int flags;
    int size;

    if (!Jget_str (mo, "name", &name) || !Jget_int (mo, "flags", &flags)
                                      || !Jget_int (mo, "size", &size))
        msg_exit ("error parsing lsmod response");

    printf ("%-20.20s %6d %-6s\n", key, size,
            (flags & FLUX_MOD_FLAGS_MANAGED) ? "m" : "");
}

static void mod_ls (flux_t h, int rank, int argc, char **argv)
{
    JSON mods;
    json_object_iter iter;

    if (argc != 1)
        usage ();

    if (!(mods = flux_lsmod (h, rank)))
        err_exit ("flux_lsmod");
    printf ("%-20s %6s %s\n", "Module", "Size", "Flags");
    json_object_object_foreachC (mods, iter) {
        list_module (iter.key, iter.val);
    }
    Jput (mods);
}

#define RM_OPTS "m"
static const struct option rm_opts[] = {
    {"managed",       no_argument,        0, 'm'},
    { 0, 0, 0, 0 },
};

static void mod_rm (flux_t h, int rank, int argc, char **argv)
{
    int i;
    int flags = 0;
    int ch;
    bool mflag = false;

    optind = 0;
    opterr = 0;
    while ((ch = getopt_long (argc, argv, RM_OPTS, rm_opts, NULL)) != -1) {
        switch (ch) {
            case 'm': /* --managed */
                mflag = true;
                break;
            default:
                usage ();
        }
    }
    if (optind == argc)
       usage ();
    for (i = optind; i < argc; i++) {
        if (mflag) {
            char *key;
            if (asprintf (&key, "conf.modctl.%s", argv[i]) < 0)
                oom (); 
            if (kvs_unlink (h, key) < 0)
                err_exit ("%s", key);
            if (kvs_commit (h) < 0)
                err_exit ("kvs_commit");
            msg ("%s: unloaded", argv[i]);
            free (key);
        } else {
            if (flux_rmmod (h, rank, argv[i], flags) < 0)
                err ("%s", argv[i]);
            else
                msg ("%s: unloaded", argv[i]);
        }
    }
}

static char *modname (const char *path)
{
    void *dso;
    char *s = NULL;
    const char **np;

    if (!(dso = dlopen (path, RTLD_NOW | RTLD_LOCAL)))
        goto done;
    if (!(np = dlsym (dso, "mod_name")) || !*np)
        goto done;
    s = xstrdup (*np);
done:
    if (dso)
        dlclose (dso);
    return s;
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

static int read_all (int fd, uint8_t **bufp)
{
    const int chunksize = 4096;
    int len = 0;
    uint8_t *buf = NULL;
    int n;
    int count = 0;

    do {
        if (len - count == 0) {
            len += chunksize;
            if (!(buf = buf ? realloc (buf, len) : malloc (len)))
                goto nomem;
        }
        if ((n = read (fd, buf + count, len - count)) < 0) {
            free (buf);
            return n;
        }
        count += n;
    } while (n != 0);
    *bufp = buf;
    return count;
nomem:
    errno = ENOMEM;
    return -1;
}

#define INS_OPTS "m"
static const struct option ins_opts[] = {
    {"managed",       no_argument,        0, 'm'},
    { 0, 0, 0, 0 },
};

static void mod_ins (flux_t h, int rank, int argc, char **argv)
{
    JSON args = Jnew ();
    int i;
    char *path, *trypath = NULL;
    int flags = 0;
    bool mflag = false;
    int ch;

    optind = 0;
    opterr = 0;
    while ((ch = getopt_long (argc, argv, INS_OPTS, ins_opts, NULL)) != -1) {
        switch (ch) {
            case 'm': /* --managed */
                mflag = true;
                break;
            default:
                usage ();
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
    for (i = optind; i < argc; i++) {
        char *val, *cpy = xstrdup (argv[i]);
        if ((val == strchr (cpy, '=')))
            *val++ = '\0';
        if (!val)
            msg_exit ("malformed argument: %s", cpy);
        Jadd_str (args, cpy, val);
        free (cpy);
    }
    if (mflag) {
        char *name;
        JSON mod;
        int fd, len;
        uint8_t *buf;
        char *key;

        if (!(name = modname (path)))
            msg_exit ("%s: mod_name undefined", path);
        if (asprintf (&key, "conf.modctl.%s", name) < 0)
            oom (); 
        if (kvs_get (h, key, &mod) == 0)
            errn_exit (EEXIST, "%s", key);

        mod = Jnew ();
        Jadd_obj (mod, "args", args);
        if ((fd = open (path, O_RDONLY)) < 0)
            err_exit ("%s", path);
        if ((len = read_all (fd, &buf)) < 0)
            err_exit ("%s", path);
        (void)close (fd);
        util_json_object_add_data (mod, "data", buf, len);

        if (kvs_put (h, key, mod) < 0)
            err_exit ("kvs_put %s", key);
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");

        free (key);
        free (buf);
        free (name);
        Jput (mod); 
    } else {
        if (flux_insmod (h, rank, path, flags, args) < 0)
            err_exit ("%s", path);
    }
    Jput (args);
    msg ("module loaded");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
