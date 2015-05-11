/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <getopt.h>
#include <dlfcn.h>
#include <json.h>
#include <flux/core.h>
#include <assert.h>

#include "src/modules/modctl/modctl.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/readall.h"
#include "src/common/libutil/nodeset.h"

const int max_idle = 99;

typedef struct {
    int fanout;
    bool direct;
    char *nodeset;
    int argc;
    char **argv;
} opt_t;

#define OPTIONS "+hr:df:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"fanout",     required_argument,  0, 'f'},
    {"direct",     no_argument,        0, 'd'},
    { 0, 0, 0, 0 },
};

void mod_lsmod (flux_t h, opt_t opt);
void mod_rmmod (flux_t h, opt_t opt);
void mod_insmod (flux_t h, opt_t opt);
void mod_info (flux_t h, opt_t opt);

typedef struct {
    const char *name;
    void (*fun)(flux_t h, opt_t opt); 
} func_t;

static func_t funcs[] = {
    { "list",   &mod_lsmod},
    { "remove", &mod_rmmod},
    { "load",   &mod_insmod},
    { "info",   &mod_info},
};

func_t *func_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (funcs) / sizeof (funcs[0]); i++)
        if (!strcmp (funcs[i].name, name))
            return &funcs[i];
    return NULL;
}

void usage (void)
{
    fprintf (stderr,
"Usage: flux-module list   [OPTIONS]\n"
"       flux-module info   [OPTIONS] module\n"
"       flux-module load   [OPTIONS] module [arg ...]\n"
"       flux-module unload [OPTIONS] module\n"
"where OPTIONS are:\n"
"       -r,--rank=[ns|all]  specify nodeset where op will be performed\n"
"       -d,--direct         bypass modctl and KVS, with decreased scalability\n"
"       -f,--fanout N       specify max concurrency in direct mode (def: 1024)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h = NULL;
    int ch;
    char *cmd;
    func_t *f;
    opt_t opt = {
        .fanout = 1024,
        .nodeset = NULL,
    };

    log_init ("flux-module");

    if (argc < 2)
        usage ();
    cmd = argv[1];
    argc--;
    argv++;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank=[nodeset|all] */
                if (opt.nodeset)
                    free (opt.nodeset);
                opt.nodeset = xstrdup (optarg);
                break;
            case 'd': /* --direct */
                opt.direct = true;
                break;
            case 'f': /* --fanout */
                opt.fanout = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    opt.argc = argc - optind;
    opt.argv = argv + optind;

    if (!(f = func_lookup (cmd)))
        msg_exit ("unknown function '%s'", cmd);

    if (strcmp (cmd, "info") != 0) {
        if (!(h = flux_open (NULL, 0)))
            err_exit ("flux_open");
        if (!opt.nodeset) {
            opt.nodeset = xasprintf ("%d", flux_rank (h));
        } else if (!strcmp (opt.nodeset, "all") && flux_size (h) == 1) {
            free (opt.nodeset);
            opt.nodeset= xasprintf ("%d", flux_rank (h));
        } else if (!strcmp (opt.nodeset, "all")) {
            free (opt.nodeset);
            opt.nodeset = xasprintf ("[0-%d]", flux_size (h) - 1);
        }
    }

    f->fun (h, opt);

    if (opt.nodeset)
        free (opt.nodeset);
    if (h)
        flux_close (h);

    log_fini ();
    return 0;
}

char *sha1 (const char *path)
{
    zfile_t *zf = zfile_new (NULL, path);
    char *digest = NULL;
    if (zf)
        digest = xstrdup (zfile_digest (zf));
    zfile_destroy (&zf);
    return digest;
}

int filesize (const char *path)
{
    struct stat sb;
    if (stat (path, &sb) < 0)
        return 0;
    return sb.st_size;
}

void mod_info (flux_t h, opt_t opt)
{
    char *modpath = NULL;
    char *modname = NULL;
    char *digest = NULL;

    if (opt.argc != 1)
        usage ();
    if (strchr (opt.argv[0], '/')) {
        if (!(modpath = realpath (opt.argv[0], NULL)))
            oom ();
        if (!(modname = flux_modname (modpath)))
            msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            searchpath = MODULE_PATH;
        modname = xstrdup (opt.argv[0]);
        if (!(modpath = flux_modfind (searchpath, modname)))
            msg_exit ("%s: not found in module search path", modname);
    }
    digest = sha1 (modpath);
    printf ("Module name:  %s\n", modname);
    printf ("Module path:  %s\n", modpath);
    printf ("SHA1 Digest:  %s\n", digest);
    printf ("Size:         %d bytes\n", filesize (modpath));

    if (modpath)
        free (modpath);
    if (modname)
        free (modname);
    if (digest)
        free (digest);
}

char *getservice (const char *modname)
{
    char *service = NULL;
    if (strchr (modname, '.')) {
        service = xstrdup (modname);
        char *p = strrchr (service, '.');
        *p = '\0';
    } else
        service = xstrdup ("cmb");
    return service;
}

void mod_insmod (flux_t h, opt_t opt)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (opt.argc < 1)
        usage ();
    if (strchr (opt.argv[0], '/')) {
        if (!(modpath = realpath (opt.argv[0], NULL)))
            oom ();
        if (!(modname = flux_modname (modpath)))
            msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            searchpath = MODULE_PATH;
        modname = xstrdup (opt.argv[0]);
        if (!(modpath = flux_modfind (searchpath, modname)))
            msg_exit ("%s: not found in module search path", modname);
    }
    opt.argv++;
    opt.argc--;
    if (opt.direct) {
        char *service = getservice (modname);
        char *topic = xasprintf ("%s.insmod", service);
        JSON in = flux_insmod_json_encode (modpath, opt.argc, opt.argv);
        if (flux_json_multrpc (h, opt.nodeset, opt.fanout, topic, in,
                                                            NULL, NULL) < 0)
            err_exit ("%s", modname);
        free (topic);
        free (service);
        Jput (in);
    } else {
        if (flux_modctl_load (h, opt.nodeset, modpath, opt.argc, opt.argv) < 0)
            err_exit ("%s", modname);
    }
    if (modpath)
        free (modpath);
    if (modname)
        free (modname);
}

void mod_rmmod (flux_t h, opt_t opt)
{
    char *modname = NULL;

    if (opt.argc != 1)
        usage ();
    modname = opt.argv[0];
    if (opt.direct) {
        char *service = getservice (modname);
        char *topic = xasprintf ("%s.rmmod", service);
        JSON in = flux_rmmod_json_encode (modname);
        if (flux_json_multrpc (h, opt.nodeset, opt.fanout, topic, in,
                                                            NULL, NULL) < 0)
            err_exit ("%s", modname);
        free (topic);
        free (service);
        Jput (in);
    } else {
        if (flux_modctl_unload (h, opt.nodeset, modname) < 0)
            err_exit ("%s", modname);
    }
}

int lsmod_print_cb (const char *name, int size, const char *digest, int idle,
                    const char *nodeset, void *arg)
{
    int digest_len = strlen (digest);
    char idle_str[16];
    if (idle <= max_idle)
        snprintf (idle_str, sizeof (idle_str), "%d", idle);
    else
        strncpy (idle_str, "idle", sizeof (idle_str));
    printf ("%-20.20s %6d %7s %4s %s\n", name, size,
            digest_len > 7 ? digest + digest_len - 7 : digest,
            idle_str, nodeset ? nodeset : "");
    return 0;
}

typedef struct {
    char *name;
    int size;
    char *digest;
    int idle;
    nodeset_t nodeset;
} mod_t;

void mod_destroy (mod_t *m)
{
    free (m->name);
    free (m->digest);
    nodeset_destroy (m->nodeset);
    free (m);
}

mod_t *mod_create (const char *name, int size, const char *digest,
                          int idle, uint32_t nodeid)
{
    mod_t *m = xzmalloc (sizeof (*m));
    m->name = xstrdup (name);
    m->size = size;
    m->digest = xstrdup (digest);
    m->idle = idle;
    if (!(m->nodeset = nodeset_new_rank (nodeid)))
        oom ();
    return m;
}

void lsmod_map_hash (zhash_t *mods, flux_lsmod_f cb, void *arg)
{
    zlist_t *keys = NULL;
    const char *key;
    mod_t *m;
    int errnum = 0;

    if (!(keys = zhash_keys (mods)))
        oom ();
    key = zlist_first (keys);
    while (key != NULL) {
        if ((m = zhash_lookup (mods, key))) {
            if (cb (m->name, m->size, m->digest, m->idle,
                                       nodeset_str (m->nodeset), arg) < 0) {
                if (errno > errnum)
                    errnum = errno;
            }
        }
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);
}

int lsmod_hash_cb (uint32_t nodeid, uint32_t errnum, JSON out, void *arg)
{
    zhash_t *mods = arg;
    mod_t *m;
    int i, len;
    const char *name, *digest;
    int size, idle;

    if (errnum)
        return 0;
    if (flux_lsmod_json_decode (out, &len) < 0)
        return -1;
    for (i = 0; i < len; i++) {
        if (flux_lsmod_json_decode_nth (out, i, &name, &size, &digest,
                                                                &idle) < 0)
            return -1;
        if ((m = zhash_lookup (mods, digest))) {
            if (idle < m->idle)
                m->idle = idle;
            if (!nodeset_add_rank (m->nodeset, nodeid))
                oom ();
        } else {
            m = mod_create (name, size, digest, idle, nodeid);
            zhash_update (mods, digest, m);
            zhash_freefn (mods, digest, (zhash_free_fn *)mod_destroy);
        }
    }
    return 0;
}

void mod_lsmod (flux_t h, opt_t opt)
{
    char *service = "cmb";

    if (opt.argc > 1)
        usage ();
    if (opt.argc == 1)
        service = opt.argv[0];
    printf ("%-20s %6s %7s %4s %s\n",
            "Module", "Size", "Digest", "Idle", "Nodeset");
    if (opt.direct) {
        zhash_t *mods = zhash_new ();
        char *topic = xasprintf ("%s.lsmod", service);

        if (!mods)
            oom ();
        if (flux_json_multrpc (h, opt.nodeset, opt.fanout, topic, NULL,
                                                    lsmod_hash_cb, mods) < 0)
            err_exit ("modctl_list");
        lsmod_map_hash (mods, lsmod_print_cb, NULL);
        zhash_destroy (&mods);
        free (topic);
    } else {
        if (flux_modctl_list (h, service, opt.nodeset, lsmod_print_cb, NULL) < 0)
            err_exit ("modctl_list");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
