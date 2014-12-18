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


#define OPTIONS "+hr:d"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"direct",     no_argument,        0, 'd'},
    { 0, 0, 0, 0 },
};

void mod_lsmod (flux_t h, bool direct, const char *rankopt, int ac, char **av);
void mod_rmmod (flux_t h, bool direct, const char *rankopt, int ac, char **av);
void mod_insmod (flux_t h, bool direct, const char *rankopt, int ac, char **av);
void mod_info (flux_t h, bool direct, const char *rankopt, int ac, char **av);

typedef struct {
    const char *name;
    void (*fun)(flux_t h, bool direct, const char *rankopt, int ac, char **av);
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
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h = NULL;
    int ch;
    char *cmd;
    func_t *f;
    bool direct = false;
    char *rankopt = NULL;

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
                rankopt = xstrdup (optarg);
                break;
            case 'd': /* --direct */
                direct = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (!(f = func_lookup (cmd)))
        msg_exit ("unknown function '%s'", cmd);

    if (strcmp (cmd, "info") != 0) {
        if (!(h = flux_api_open ()))
            err_exit ("flux_api_open");
        if (!rankopt) {
            rankopt = xasprintf ("%d", flux_rank (h));
        } else if (!strcmp (rankopt, "all") && flux_size (h) == 1) {
            free (rankopt);
            rankopt = xasprintf ("%d", flux_rank (h));
        } else if (!strcmp (rankopt, "all")) {
            free (rankopt);
            rankopt = xasprintf ("[0-%d]", flux_size (h) - 1);
        }
    }

    f->fun (h, direct, rankopt, argc - optind, argv + optind);

    if (rankopt)
        free (rankopt);
    if (h)
        flux_api_close (h);

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

void mod_info (flux_t h, bool direct, const char *rankopt, int ac, char **av)
{
    char *modpath = NULL;
    char *modname = NULL;
    char *digest = NULL;

    if (ac != 1)
        usage ();
    if (strchr (av[0], '/')) {                /* path name given */
        if (!(modpath = realpath (av[0], NULL)))
            oom ();
        if (!(modname = flux_modname (modpath)))
            msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            searchpath = MODULE_PATH;
        modname = xstrdup (av[0]);
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

/* Iteratively call flux_insmod().
 * Return 0 on success, -1 on failure with errno set.
 * If an error is returned by one or more flux_insmod calls, continue
 * iterating and report the 1st errno for consistency with flux_modctl_load().
 */
int iter_insmod (flux_t h, nodeset_t nodeset, const char *path,
                 int ac, char **av)
{
    nodeset_itr_t itr;
    uint32_t nodeid;
    int errnum = 0;
    int rc = 0;

    if (!(itr = nodeset_itr_new (nodeset)))
        msg_exit ("error creating nodeset iterator");
    while ((nodeid = nodeset_next (itr)) != NODESET_EOF) {
        if (flux_insmod (h, nodeid, path, ac, av) < 0) {
            if (errnum == 0)
                errnum = errno;
            rc = -1;
        }
    }
    nodeset_itr_destroy (itr);
    if (rc < 0)
        errno = errnum;
    return rc;
}

void mod_insmod (flux_t h, bool direct, const char *ns, int ac, char **av)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (ac < 1)
        usage ();
    if (strchr (av[0], '/')) {                /* path name given */
        if (!(modpath = realpath (av[0], NULL)))
            oom ();
        if (!(modname = flux_modname (modpath)))
            msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            searchpath = MODULE_PATH;
        modname = xstrdup (av[0]);
        if (!(modpath = flux_modfind (searchpath, modname)))
            msg_exit ("%s: not found in module search path", modname);
    }
    if (direct) {
        nodeset_t nodeset = nodeset_new_str (ns);
        if (!nodeset)
            msg_exit ("error parsing nodeset: %s", ns);
        if (iter_insmod (h, nodeset, modpath, ac - 1, av + 1) < 0)
            err_exit ("%s", modname);
        nodeset_destroy (nodeset);
    } else {
        if (flux_modctl_load (h, ns, modpath, ac - 1, av + 1) < 0)
            err_exit ("%s", modname);
    }
    if (modpath)
        free (modpath);
    if (modname)
        free (modname);
}

int iter_rmmod (flux_t h, nodeset_t nodeset, const char *modname)
{
    nodeset_itr_t itr;
    uint32_t nodeid;
    int errnum = 0;
    int rc = 0;

    if (!(itr = nodeset_itr_new (nodeset)))
        msg_exit ("error creating nodeset iterator");
    while ((nodeid = nodeset_next (itr)) != NODESET_EOF) {
        if (flux_rmmod (h, nodeid, modname) < 0) {
            if (errnum == 0)
                errnum = errno;
            rc = -1;
        }
    }
    nodeset_itr_destroy (itr);
    if (rc < 0)
        errno = errnum;
    return rc;
}

void mod_rmmod (flux_t h, bool direct, const char *ns, int ac, char **av)
{
    char *modname = NULL;

    if (ac != 1)
        usage ();
    modname = av[0];
    if (direct) {
        nodeset_t nodeset = nodeset_new_str (ns);
        if (!nodeset)
            msg_exit ("error parsing nodeset: %s", ns);
        if (iter_rmmod (h, nodeset, modname) < 0)
            err_exit ("%s", modname);
        nodeset_destroy (nodeset);
    } else {
        if (flux_modctl_unload (h, ns, modname) < 0)
            err_exit ("modctl_unload %s", modname);
    }
}

int lsmod_cb (const char *name, int size, const char *digest, int idle,
              const char *nodeset, void *arg)
{
    int digest_len = strlen (digest);
    char idle_str[16];
    if (idle < 100)
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

typedef struct {
    zhash_t *mod;
    zlist_t *keys;
    char *key;
    uint32_t nodeid;
} iter_lsmod_ctx_t;

int iter_lsmod_cb (const char *name, int size, const char *digest,
                   int idle, const char *nodeset, void *arg)
{
    iter_lsmod_ctx_t *ctx = arg;
    mod_t *m;

    if ((m = zhash_lookup (ctx->mod, digest))) {
        if (idle < m->idle)
            m->idle = idle;
        if (!nodeset_add_rank (m->nodeset, ctx->nodeid))
            oom ();
    } else {
        m = mod_create (name, size, digest, idle, ctx->nodeid);
        zhash_update (ctx->mod, digest, m);
        zhash_freefn (ctx->mod, digest, (zhash_free_fn *)mod_destroy);
    }
    return 0;
}

int iter_lsmod (flux_t h, nodeset_t nodeset, const char *svc,
                flux_lsmod_f cb, void *arg)
{
    nodeset_itr_t itr;
    uint32_t nodeid;
    int errnum = 0;
    int rc = 0;
    iter_lsmod_ctx_t ctx;

    if (!(ctx.mod = zhash_new ()))
        oom ();
    if (!(itr = nodeset_itr_new (nodeset)))
        msg_exit ("error creating nodeset iterator");
    while ((nodeid = nodeset_next (itr)) != NODESET_EOF) {
        ctx.nodeid = nodeid;
        if (flux_lsmod (h, nodeid, svc, iter_lsmod_cb, &ctx) < 0) {
            if (errnum == 0)
                errnum = errno;
            rc = -1;
        }
    }
    nodeset_itr_destroy (itr);
    if (!(ctx.keys = zhash_keys (ctx.mod)))
        oom ();
    ctx.key = zlist_first (ctx.keys);
    while (ctx.key) {
        mod_t *m = zhash_lookup (ctx.mod, ctx.key);
        assert (m != NULL);
        const char *ns = nodeset_str (m->nodeset);
        if (cb (m->name, m->size, m->digest, m->idle, ns, arg) < 0) {
            rc = -1;
            break;
        }
        ctx.key = zlist_next (ctx.keys);
    }
    zlist_destroy (&ctx.keys);
    zhash_destroy (&ctx.mod);
    if (rc < 0)
        errno = errnum;
    return rc;
}

void mod_lsmod (flux_t h, bool direct, const char *ns, int ac, char **av)
{
    char *svc = "cmb";

    if (ac > 1)
        usage ();
    if (ac == 1)
        svc = av[0];
    printf ("%-20s %6s %7s %4s %s\n",
            "Module", "Size", "Digest", "Idle", "Nodeset");
    if (direct) {
        nodeset_t nodeset = nodeset_new_str (ns);
        if (!nodeset)
            msg_exit ("error parsing nodeset: %s", ns);
        if (iter_lsmod (h, nodeset, svc, lsmod_cb, NULL) < 0)
            err_exit ("%s", svc);
        nodeset_destroy (nodeset);
    } else {
        if (flux_modctl_list (h, svc, ns, lsmod_cb, NULL) < 0)
            err_exit ("modctl_list");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
