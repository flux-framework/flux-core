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
#include <flux/core.h>
#include <czmq.h>
#include <assert.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/readall.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/iterators.h"

const int max_idle = 99;

typedef struct {
    const char *nodeset;
    int argc;
    char **argv;
} opt_t;

#define OPTIONS "+hr:x:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"exclude",    required_argument,  0, 'x'},
    { 0, 0, 0, 0 },
};

void mod_lsmod (flux_t *h, flux_extensor_t *ex, opt_t opt);
void mod_rmmod (flux_t *h, flux_extensor_t *ex, opt_t opt);
void mod_insmod (flux_t *h, flux_extensor_t *ex, opt_t opt);
void mod_info (flux_t *h, flux_extensor_t *ex, opt_t opt);

typedef struct {
    const char *name;
    void (*fun)(flux_t *h, flux_extensor_t *ex, opt_t opt);
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
"       flux-module remove [OPTIONS] module\n"
"where OPTIONS are:\n"
"       -r,--rank=NODESET     add ranks (default \"self\") \n"
"       -x,--exclude=NODESET  exclude ranks\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    flux_extensor_t *ex = NULL;
    int ch;
    char *cmd;
    func_t *f;
    opt_t opt;
    const char *rankopt = "self";
    const char *excludeopt = NULL;

    log_init ("flux-module");

    memset (&opt, 0, sizeof (opt));
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
            case 'r': /* --rank=NODESET */
                rankopt = optarg;
                break;
            case 'x': /* --exclude=NODESET */
                excludeopt = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    opt.argc = argc - optind;
    opt.argv = argv + optind;

    if (!(f = func_lookup (cmd)))
        log_msg_exit ("unknown function '%s'", cmd);

    if (!(ex = flux_extensor_create ()))
        log_msg_exit ("failed to create flux extensor");

    if (strcmp (cmd, "info") != 0) {
        if (!(h = flux_open (NULL, 0)))
            log_err_exit ("flux_open");
        if (!(opt.nodeset = flux_get_nodeset (h, rankopt, excludeopt)))
            log_err_exit ("--exclude/--rank");
        if (strlen (opt.nodeset) == 0)
            exit (0);
    }

    f->fun (h, ex, opt);

    if (h)
        flux_close (h);

    flux_extensor_destroy (ex);

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

flux_module_t * find_module (flux_extensor_t *ex, const char *arg)
{
    char *searchpath = getenv ("FLUX_MODULE_PATH");
    if (!searchpath)
        log_msg_exit ("FLUX_MODULE_PATH is not set");
    return flux_extensor_load_module (ex, searchpath, arg);
}

void mod_info (flux_t *h, flux_extensor_t *ex, opt_t opt)
{
    flux_module_t *m;
    const char *arg = opt.argv[0];
    char *digest = NULL;

    if (opt.argc != 1)
        usage ();
    if (!(m = find_module (ex, arg)))
        log_msg_exit ("Failed to find module %s", arg);

    digest = sha1 (flux_module_path (m));
    printf ("Module name:  %s\n", flux_module_name (m));
    printf ("Module path:  %s\n", flux_module_path (m));
    printf ("SHA1 Digest:  %s\n", digest);
    printf ("Size:         %d bytes\n", filesize (flux_module_path (m)));

    free (digest);
}

/* Derive name of module loading service from module name.
 */
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

void mod_insmod (flux_t *h, flux_extensor_t *ex, opt_t opt)
{
    flux_rpc_t *r;
    flux_module_t *m;
    int errors = 0;

    if (opt.argc < 1)
        usage ();
    if (!(m = find_module (ex, opt.argv[0])))
        log_msg_exit ("%s: not found in module search path", opt.argv[0]);
    opt.argv++;
    opt.argc--;

    if (!(r = flux_module_insmod_rpc (m, h, opt.nodeset, opt.argc, opt.argv)))
        log_err_exit ("%s.insmod", flux_module_service (m));
    do {
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
                                    || flux_rpc_get (r, NULL) < 0) {
            if (errno == EEXIST && nodeid != FLUX_NODEID_ANY)
                log_msg ("%s[%" PRIu32 "]: %s module/service is in use",
                     flux_module_service (m), nodeid, flux_module_name (m));
            else if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", flux_module_service (m), nodeid);
            else
                log_err ("%s.insmod", flux_module_service (m));
            errors++;
        }
    } while (flux_rpc_next (r) == 0);
    flux_rpc_destroy (r);
    if (errors)
        exit (1);
}

void mod_rmmod (flux_t *h, flux_extensor_t *ex, opt_t opt)
{
    char *modname = NULL;

    if (opt.argc != 1)
        usage ();
    modname = opt.argv[0];

    char *service = getservice (modname);
    char *topic = xasprintf ("%s.rmmod", service);
    char *json_str = flux_rmmod_json_encode (modname);
    assert (json_str != NULL);
    flux_rpc_t *r = flux_rpc_multi (h, topic, json_str, opt.nodeset, 0);
    if (!r)
        log_err_exit ("%s %s", topic, modname);
    do {
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_rpc_get_nodeid (r, &nodeid) < 0 || flux_rpc_get (r, NULL) < 0)
            log_err ("%s[%d] %s",
                 topic, nodeid == FLUX_NODEID_ANY ? -1 : nodeid,
                 modname);
    } while (flux_rpc_next (r) == 0);
    flux_rpc_destroy (r);
    free (topic);
    free (service);
    free (json_str);
}

int lsmod_print_cb (const char *name, int size, const char *digest, int idle,
                    int status, const char *nodeset, void *arg)
{
    int digest_len = strlen (digest);
    char idle_str[16];
    char S;
    if (idle <= max_idle)
        snprintf (idle_str, sizeof (idle_str), "%d", idle);
    else
        strncpy (idle_str, "idle", sizeof (idle_str));
    switch (status) {
        case FLUX_MODSTATE_INIT:
            S ='I';
            break;
        case FLUX_MODSTATE_SLEEPING:
            S ='S';
            break;
        case FLUX_MODSTATE_RUNNING:
            S ='R';
            break;
        case FLUX_MODSTATE_FINALIZING:
            S ='F';
            break;
        case FLUX_MODSTATE_EXITED:
            S ='X';
            break;
        default:
            S = '?';
            break;
    }
    printf ("%-20.20s %7d %7s %4s  %c  %s\n", name, size,
            digest_len > 7 ? digest + digest_len - 7 : digest,
            idle_str, S, nodeset ? nodeset : "");
    return 0;
}

typedef struct {
    char *name;
    int size;
    char *digest;
    int idle;
    int status_hist[8];
    nodeset_t *nodeset;
} mod_t;

void mod_destroy (mod_t *m)
{
    free (m->name);
    free (m->digest);
    nodeset_destroy (m->nodeset);
    free (m);
}

mod_t *mod_create (const char *name, int size, const char *digest,
                   int idle, int status, uint32_t nodeid)
{
    mod_t *m = xzmalloc (sizeof (*m));
    m->name = xstrdup (name);
    m->size = size;
    m->digest = xstrdup (digest);
    m->idle = idle;
    m->status_hist[abs (status) % 8]++;
    if (!(m->nodeset = nodeset_create_rank (nodeid)))
        oom ();
    return m;
}

void lsmod_map_hash (zhash_t *mods, flux_lsmod_f cb, void *arg)
{
    const char *key;
    mod_t *m;
    int status;

    FOREACH_ZHASH(mods, key, m) {
        if (m->status_hist[FLUX_MODSTATE_INIT] > 0)
            status = FLUX_MODSTATE_INIT;
        else if (m->status_hist[FLUX_MODSTATE_EXITED] > 0) /* unlikely */
            status = FLUX_MODSTATE_EXITED;
        else if (m->status_hist[FLUX_MODSTATE_FINALIZING] > 0)
            status = FLUX_MODSTATE_FINALIZING;
        else if (m->status_hist[FLUX_MODSTATE_RUNNING] > 0)
            status = FLUX_MODSTATE_RUNNING;
        else
            status = FLUX_MODSTATE_SLEEPING;
        cb (m->name, m->size, m->digest, m->idle, status,
                                        nodeset_string (m->nodeset), arg);
    }
}

int lsmod_merge_result (uint32_t nodeid, const char *json_str, zhash_t *mods)
{
    flux_modlist_t *modlist;
    mod_t *m;
    int i, len;
    const char *name, *digest;
    int size, idle;
    int status;
    int rc = -1;

    if (!(modlist = flux_lsmod_json_decode (json_str)))
        goto done;
    if ((len = flux_modlist_count (modlist)) == -1)
        goto done;
    for (i = 0; i < len; i++) {
        if (flux_modlist_get (modlist, i, &name, &size, &digest, &idle,
                                                                 &status) < 0)
            goto done;
        if ((m = zhash_lookup (mods, digest))) {
            if (idle < m->idle)
                m->idle = idle;
            m->status_hist[abs (status) % 8]++;
            if (!nodeset_add_rank (m->nodeset, nodeid))
                oom ();
        } else {
            m = mod_create (name, size, digest, idle, status, nodeid);
            zhash_update (mods, digest, m);
            zhash_freefn (mods, digest, (zhash_free_fn *)mod_destroy);
        }
    }
    rc = 0;
done:
    if (modlist)
        flux_modlist_destroy (modlist);
    return rc;
}

void mod_lsmod (flux_t *h, flux_extensor_t *ex, opt_t opt)
{
    char *service = "cmb";

    if (opt.argc > 1)
        usage ();
    if (opt.argc == 1)
        service = opt.argv[0];
    printf ("%-20s %-7s %-7s %4s  %c  %s\n",
            "Module", "Size", "Digest", "Idle", 'S', "Nodeset");
    zhash_t *mods = zhash_new ();
    if (!mods)
        oom ();
    char *topic = xasprintf ("%s.lsmod", service);
    flux_rpc_t *r = flux_rpc_multi (h, topic, NULL, opt.nodeset, 0);
    if (!r)
        log_err_exit ("%s", topic);
    do {
        const char *json_str;
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
                || flux_rpc_get (r, &json_str) < 0
                || lsmod_merge_result (nodeid, json_str, mods) < 0) {
            if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", topic, nodeid);
            else
                log_err ("%s", topic);
        }
    } while (flux_rpc_next (r) == 0);
    flux_rpc_destroy (r);
    lsmod_map_hash (mods, lsmod_print_cb, NULL);
    zhash_destroy (&mods);
    free (topic);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
