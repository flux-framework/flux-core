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

#define OPTIONS "+hr:u"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"unmanaged",  no_argument,        0, 'u'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

static void mod_ls (flux_t h, int rank, int argc, char **argv);
static void mod_rm (flux_t h, int rank, int argc, char **argv);
static void mod_ins (flux_t h, int rank, int argc, char **argv);

static void mod_ls_m (flux_t h, int argc, char **argv);
static void mod_rm_m (flux_t h, int argc, char **argv);
static void mod_ins_m (flux_t h, int argc, char **argv);

static void mod_update (flux_t h);

void usage (void)
{
    fprintf (stderr,
"Usage: flux-mod [OPTIONS] ls\n"
"       flux-mod [OPTIONS] rm module [module...]\n"
"       flux-mod [OPTIONS] ins module [arg=val ...]\n"
"       flux-mod update\n"
"Options:\n"
"  -u,--unmanaged      act locally, do not set/require 'm' flag\n"
"  -r,--rank=N         act on specified rank (requires -u)\n"

);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int rank = -1;
    bool uopt = false;
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
            case 'u': /* --unmanaged */
                uopt = true;
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
    if (rank != -1 && uopt == false)
        usage ();

    if (!strcmp (cmd, "ls")) {
        if (uopt)
            mod_ls (h, rank, argc - optind, argv + optind);
        else
            mod_ls_m (h, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "rm")) {
        if (uopt)
            mod_rm (h, rank, argc - optind, argv + optind);
        else
            mod_rm_m (h, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "ins")) {
        if (uopt)
            mod_ins (h, rank, argc - optind, argv + optind);
        else
            mod_ins_m (h, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "update")) {
        mod_update (h);
    } else
        usage ();

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static char *flagstr (int flags)
{
    char *s = xzmalloc (16);
    if ((flags & FLUX_MOD_FLAGS_MANAGED))
        strcat (s, "m");
    return s;
}

static char *idlestr (int idle)
{
    char *s;
    if (idle > 99)
        s = xstrdup ("idle");
    else if (asprintf (&s, "%d", idle) < 0)
        oom ();
    return s;
}

static void list_module (const char *key, JSON mo)
{
    const char *name, *nodelist = NULL;
    int flags, idle, size;
    char *fs, *is;

    if (!Jget_str (mo, "name", &name) || !Jget_int (mo, "flags", &flags)
     || !Jget_int (mo, "size", &size) || !Jget_str (mo, "nodelist", &nodelist)
     || !Jget_int (mo, "idle", &idle))
        msg_exit ("error parsing lsmod response");
    fs = flagstr (flags);
    is = idlestr (idle);
    printf ("%-20.20s %6d %-6s %4s %s\n", key, size, fs, is, nodelist);
    free (fs);
    free (is);
}

static void mod_ls (flux_t h, int rank, int argc, char **argv)
{
    JSON mods, mod;
    json_object_iter iter;
    int i;

    if (!(mods = flux_lsmod (h, rank)))
        err_exit ("flux_lsmod");
    printf ("%-20s %6s %-6s %4s %s\n",
            "Module", "Size", "Flags", "Idle", "Nodelist");
    if (argc == 0) {
        json_object_object_foreachC (mods, iter) {
            list_module (iter.key, iter.val);
        }
    } else {
        for (i = 0; i < argc; i++) {
            if (!Jget_obj (mods, argv[i], &mod))
                printf ("%s: not loaded\n", argv[i]);
            else
                list_module (argv[i], mod);
        }
    }
    Jput (mods);
}

static void mod_ls_m (flux_t h, int argc, char **argv)
{
    JSON lsmod, mods;
    json_object_iter iter;

    printf ("%-20s %6s %-6s %4s %s\n",
            "Module", "Size", "Flags", "Idle", "Nodelist");
    if (kvs_get (h, "conf.modctl.lsmod", &lsmod) == 0) {
        if (!Jget_obj (lsmod, "mods", &mods))
            msg_exit ("error parsing lsmod KVS object");
        json_object_object_foreachC (mods, iter) {
            list_module (iter.key, iter.val);
        }
        Jput (lsmod);
    }
}

static void mod_rm (flux_t h, int rank, int argc, char **argv)
{
    int i;

    if (argc == 0)
       usage ();
    for (i = 0; i < argc; i++) {
        if (flux_rmmod (h, rank, argv[i], 0) < 0)
            err ("%s", argv[i]);
        else
            msg ("%s: unloaded", argv[i]);
    }
}

static void mod_rm_m (flux_t h, int argc, char **argv)
{
    int i;
    char *key;

    if (argc == 0)
       usage ();
    for (i = 0; i < argc; i++) {
        if (asprintf (&key, "conf.modctl.modules.%s", argv[i]) < 0)
            oom ();
        if (kvs_unlink (h, key) < 0)
            err_exit ("%s", key);
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");
        if (flux_modctl_rm (h, argv[i]) < 0)
            err_exit ("%s", argv[i]);
        msg ("%s: unloaded", argv[i]);
        free (key);
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

static JSON parse_modargs (int argc, char **argv)
{
    JSON args = Jnew ();
    int i;

    for (i = 0; i < argc; i++) {
        char *val, *cpy = xstrdup (argv[i]);
        if ((val == strchr (cpy, '=')))
            *val++ = '\0';
        if (!val)
            msg_exit ("malformed argument: %s", cpy);
        Jadd_str (args, cpy, val);
        free (cpy);
    }

    return args;
}

static void mod_ins (flux_t h, int rank, int argc, char **argv)
{
    JSON args;
    char *path, *trypath = NULL;

    if (argc == 0)
       usage ();
    path = argv[0];
    if (access (path, R_OK|X_OK) < 0) {
        if (!(trypath = modfind (PLUGIN_PATH, path)))
            errn_exit (ENOENT, "%s", path);
        path = trypath;
    }
    args = parse_modargs (argc - 1, argv + 1);
    if (flux_insmod (h, rank, path, 0, args) < 0)
        err_exit ("%s", path);
    msg ("module loaded");

    Jput (args);
    if (trypath)
        free (trypath);
}

/* Copy mod to KVS (without commit).
 */
static void copymod (flux_t h, const char *name, const char *path, JSON args)
{
    JSON mod = Jnew ();
    char *key;
    int fd, len;
    uint8_t *buf;

    if (asprintf (&key, "conf.modctl.modules.%s", name) < 0)
        oom ();
    if (kvs_get (h, key, &mod) == 0)
        errn_exit (EEXIST, "%s", key);
    Jadd_obj (mod, "args", args);
    if ((fd = open (path, O_RDONLY)) < 0)
        err_exit ("%s", path);
    if ((len = read_all (fd, &buf)) < 0)
        err_exit ("%s", path);
    (void)close (fd);
    util_json_object_add_data (mod, "data", buf, len);
    if (kvs_put (h, key, mod) < 0)
        err_exit ("kvs_put %s", key);
    free (key);
    free (buf);
    Jput (mod);
}

static void mod_ins_m (flux_t h, int argc, char **argv)
{
    JSON args;
    char *path, *trypath = NULL;
    char *name;

    if (argc == 0)
       usage ();
    path = argv[0];
    if (access (path, R_OK|X_OK) < 0) {
        if (!(trypath = modfind (PLUGIN_PATH, path)))
            errn_exit (ENOENT, "%s", path);
        path = trypath;
    }
    if (!(name = modname (path)))
        msg_exit ("%s: mod_name undefined", path);
    args = parse_modargs (argc - 1, argv + 1);
    copymod (h, name, path, args);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (flux_modctl_ins (h, name) < 0)
        err_exit ("flux_modctl_ins %s", name);
    msg ("module loaded");

    free (name);
    Jput (args);
    if (trypath)
        free (trypath);
}

static void mod_update (flux_t h)
{
    if (flux_modctl_update (h) < 0)
        err_exit ("flux_modctl_update");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
