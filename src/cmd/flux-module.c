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
#include <flux/optparse.h>
#include <jansson.h>
#include <czmq.h>
#include <assert.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/iterators.h"

const int max_idle = 99;

typedef int (flux_lsmod_f)(const char *name, int size, const char *digest,
                           int idle, int status,
                           const char *nodeset, void *arg);

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_remove (optparse_t *p, int argc, char **argv);
int cmd_load (optparse_t *p, int argc, char **argv);
int cmd_info (optparse_t *p, int argc, char **argv);
int cmd_stats (optparse_t *p, int argc, char **argv);
int cmd_debug (optparse_t *p, int argc, char **argv);


#define RANK_OPTION { \
    .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "NODESET", \
    .usage = "Include NODESET in operation target", \
}
#define EXCLUDE_OPTION { \
    .name = "exclude", .key = 'x', .has_arg = 1, .arginfo = "NODESET", \
    .usage = "Exclude NODESET from operation target", \
}

static struct optparse_option list_opts[] =  {
    RANK_OPTION,
    EXCLUDE_OPTION,
    OPTPARSE_TABLE_END
};
static struct optparse_option remove_opts[] =  {
    RANK_OPTION,
    EXCLUDE_OPTION,
    OPTPARSE_TABLE_END
};
static struct optparse_option load_opts[] =  {
    RANK_OPTION,
    EXCLUDE_OPTION,
    OPTPARSE_TABLE_END
};
static struct optparse_option stats_opts[] =  {
    { .name = "parse", .key = 'p', .has_arg = 1, .arginfo = "OBJNAME",
      .usage = "Parse object period-delimited object name",
    },
    { .name = "scale", .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Scale numeric JSON value by N",
    },
    { .name = "type", .key = 't', .has_arg = 1, .arginfo = "int|double",
      .usage = "Convert JSON value to specified type",
    },
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "RANK",
      .usage = "Target specified rank",
    },
    { .name = "rusage", .key = 'R', .has_arg = 0,
      .usage = "Request rusage data instead of stats",
    },
    { .name = "clear", .key = 'c', .has_arg = 0,
      .usage = "Clear stats on target rank",
    },
    { .name = "clear-all", .key = 'C', .has_arg = 0,
      .usage = "Clear stats on all ranks",
    },
    OPTPARSE_TABLE_END
};
static struct optparse_option debug_opts[] = {
    { .name = "clear",  .key = 'C',  .has_arg = 0,
      .usage = "Set debug flags to 0", },
    { .name = "set",  .key = 'S',  .has_arg = 1,
      .usage = "Set debug flags to MASK", },
    { .name = "setbit",  .key = 's',  .has_arg = 1,
      .usage = "Set one debug flag to 1", },
    { .name = "clearbit",  .key = 'c',  .has_arg = 1,
      .usage = "Set one debug flag to 0", },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand subcommands[] = {
    { "list",
      "[OPTIONS] [module]",
      "List loaded modules",
      cmd_list,
      0,
      list_opts,
    },
    { "remove",
      "[OPTIONS] module",
      "Unload module",
      cmd_remove,
      0,
      remove_opts,
    },
    { "load",
      "[OPTIONS] module",
      "Load module",
      cmd_load,
      0,
      load_opts,
    },
    { "info",
      "[OPTIONS] module",
      "Display module info",
      cmd_info,
      0,
      NULL
    },
    { "stats",
      "[OPTIONS] module",
      "Display stats on module",
      cmd_stats,
      0,
      stats_opts,
    },
    { "debug",
      "[OPTIONS] module",
      "Get/set module debug flags",
      cmd_debug,
      0,
      debug_opts,
    },
    OPTPARSE_SUBCMD_END
};

int usage (optparse_t *p, struct optparse_option *o, const char *optarg)
{
    struct optparse_subcommand *s;
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "flux module subcommands:\n");
    s = subcommands;
    while (s->name) {
        fprintf (stderr, "   %-15s %s\n", s->name, s->doc);
        s++;
    }
    exit (1);
}

int main (int argc, char *argv[])
{
    optparse_t *p;
    char *cmdusage = "COMMAND [OPTIONS]";
    int optindex;
    int exitval;

    log_init ("flux-module");

    p = optparse_create ("flux-module");

    if (optparse_set (p, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if (optparse_set (p, OPTPARSE_OPTION_CB, "help", usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set() failed");

    if (optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (PRINT_SUBCMDS)");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
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

void parse_modarg (const char *arg, char **name, char **path)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (strchr (arg, '/')) {
        if (!(modpath = realpath (arg, NULL)))
            log_err_exit ("%s", arg);
        if (!(modname = flux_modname (modpath)))
            log_msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            log_msg_exit ("FLUX_MODULE_PATH is not set");
        modname = xstrdup (arg);
        if (!(modpath = flux_modfind (searchpath, modname)))
            log_msg_exit ("%s: not found in module search path", modname);
    }
    *name = modname;
    *path = modpath;
}

int cmd_info (optparse_t *p, int argc, char **argv)
{
    char *modpath = NULL;
    char *modname = NULL;
    char *digest = NULL;
    int n;

    if ((n = optparse_option_index (p)) != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    parse_modarg (argv[n], &modname, &modpath);
    digest = sha1 (modpath);
    printf ("Module name:  %s\n", modname);
    printf ("Module path:  %s\n", modpath);
    printf ("SHA1 Digest:  %s\n", digest);
    printf ("Size:         %d bytes\n", filesize (modpath));

    free (modpath);
    free (modname);
    free (digest);
    return (0);
}

static nodeset_t *ns_create_special (flux_t *h, const char *arg)
{
    nodeset_t *ns = NULL;

    if (!arg) {
        uint32_t rank;
        if (flux_get_rank (h, &rank) < 0)
            log_err_exit ("flux_get_rank");
        if (!(ns = nodeset_create_rank (rank)))
            log_msg_exit ("nodeset_create_rank failed");
    } else if (!strcmp (arg, "all")) {
        uint32_t size;
        if (flux_get_size (h, &size) < 0)
            log_err_exit ("flux_get_size");
        if (!(ns = nodeset_create_range (0, size - 1)))
            log_msg_exit ("nodeset_create_range failed");
    } else {
        if (!(ns = nodeset_create_string (arg))) {
            errno = EINVAL;
            return NULL;
        }
    }
    return ns;
}

/* Parse --rank and --exclude options, returning a nodeset.
 * If neither are specified, default to the local rank.
 * Exit with error if an option argument is malformed (or other error).
 * Return value will never be NULL, but it may be an empty nodeset.
 */
nodeset_t *parse_nodeset_options (optparse_t *p, flux_t *h)
{
    const char *nodeset = optparse_get_str (p, "rank", NULL);
    const char *exclude = optparse_get_str (p, "exclude", NULL);
    nodeset_t *ns;

    if (!(ns = ns_create_special (h, nodeset)))
        log_msg_exit ("could not parse --rank option");
    if (exclude) {
        nodeset_t *xns;
        nodeset_iterator_t *itr;
        uint32_t rank;
        if (!(xns = ns_create_special (h, exclude)))
            log_msg_exit ("could not parse --exclude option");
        if (!(itr = nodeset_iterator_create (xns)))
            log_msg_exit ("out of memmory");
        while ((rank = nodeset_next (itr)) != NODESET_EOF)
            nodeset_delete_rank (ns, rank);
        nodeset_iterator_destroy (itr);
        nodeset_destroy (xns);
    }
    return ns;
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

int cmd_load (optparse_t *p, int argc, char **argv)
{
    char *modname;
    char *modpath;
    int errors = 0;
    int n;
    flux_t *h;
    nodeset_t *ns;
    flux_mrpc_t *r = NULL;

    if ((n = optparse_option_index (p)) == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    parse_modarg (argv[n++], &modname, &modpath);

    char *service = getservice (modname);
    char *topic = xasprintf ("%s.insmod", service);
    json_t *args = json_array ();
    if (!args)
        log_msg_exit ("json_array() failed");
    while (n < argc) {
        json_t *str = json_string (argv[n]);
        if (!str || json_array_append_new (args, str) < 0)
            log_msg_exit ("json_string() or json_array_append_new() failed");
        n++;
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    ns = parse_nodeset_options (p, h);
    if (nodeset_count (ns) == 0)
        goto done;
    if (!(r = flux_mrpc_pack (h, topic, nodeset_string (ns), 0,
                              "{s:s s:O}", "path", modpath, "args", args)))
        log_err_exit ("%s", topic);
    do {
        if (flux_mrpc_get (r, NULL) < 0) {
            uint32_t nodeid = FLUX_NODEID_ANY;
            int saved_errno = errno;
            (void)flux_mrpc_get_nodeid (r, &nodeid);
            errno = saved_errno;
            if (errno == EEXIST && nodeid != FLUX_NODEID_ANY)
                log_msg ("%s[%" PRIu32 "]: %s module/service is in use",
                         topic, nodeid, modname);
            else if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", topic, nodeid);
            else
                log_err ("%s", topic);
            errors++;
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    nodeset_destroy (ns);
    free (topic);
    free (service);
    json_decref (args);
    free (modpath);
    free (modname);
    flux_close (h);
    return (errors ? 1 : 0);
}

int cmd_remove (optparse_t *p, int argc, char **argv)
{
    char *modname;
    nodeset_t *ns;
    flux_t *h;
    flux_mrpc_t *r = NULL;
    int n;

    if ((n = optparse_option_index (p)) != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    modname = argv[n++];

    char *service = getservice (modname);
    char *topic = xasprintf ("%s.rmmod", service);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    ns = parse_nodeset_options (p, h);
    if (nodeset_count (ns) == 0)
        goto done;
    if (!(r = flux_mrpc_pack (h, topic, nodeset_string (ns), 0,
                             "{s:s}", "name", modname)))
        log_err_exit ("%s %s", topic, modname);
    do {
        if (flux_mrpc_get (r, NULL) < 0) {
            uint32_t nodeid = FLUX_NODEID_ANY;
            int saved_errno = errno;
            (void)flux_mrpc_get_nodeid (r, &nodeid);
            errno = saved_errno;
            log_err ("%s[%d] %s",
                 topic, nodeid == FLUX_NODEID_ANY ? -1 : nodeid,
                 modname);
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    nodeset_destroy (ns);
    free (topic);
    free (service);
    flux_close (h);
    return (0);
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
    flux_modlist_t *modlist = NULL;
    mod_t *m;
    int i, len;
    const char *name, *digest;
    int size, idle;
    int status;
    int rc = -1;

    if (!json_str) {
        errno = EPROTO;
        goto done;
    }
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

int cmd_list (optparse_t *p, int argc, char **argv)
{
    char *service = "cmb";
    char *topic = NULL;
    nodeset_t *ns;
    flux_mrpc_t *r = NULL;
    flux_t *h;
    int n;
    zhash_t *mods = zhash_new ();

    if (!mods)
        oom ();
    if ((n = optparse_option_index (p)) < argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (n < argc)
        service = argv[n++];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    ns = parse_nodeset_options (p, h);
    if (nodeset_count (ns) == 0)
        goto done;

    printf ("%-20s %-7s %-7s %4s  %c  %s\n",
            "Module", "Size", "Digest", "Idle", 'S', "Nodeset");
    topic = xasprintf ("%s.lsmod", service);
    if (!(r = flux_mrpc (h, topic, NULL, nodeset_string (ns), 0)))
        log_err_exit ("%s", topic);
    do {
        const char *json_str;
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_mrpc_get_nodeid (r, &nodeid) < 0
                || flux_mrpc_get (r, &json_str) < 0
                || lsmod_merge_result (nodeid, json_str, mods) < 0) {
            if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", topic, nodeid);
            else
                log_err ("%s", topic);
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    nodeset_destroy (ns);
    lsmod_map_hash (mods, lsmod_print_cb, NULL);
    zhash_destroy (&mods);
    free (topic);
    flux_close (h);
    return (0);
}

static void parse_json (optparse_t *p, const char *json_str)
{
    json_t *obj, *o;
    const char *objname, *typestr;
    double scale;

    if (!(obj = json_loads (json_str, 0, NULL)))
        log_msg_exit ("error parsing JSON response");

    /* If --parse OBJNAME was provided, walk to that
     * portion of the returned object.
     */
    o = obj;
    if ((objname = optparse_get_str (p, "parse", NULL))) {
        char *cpy = xstrdup (objname);
        char *name, *saveptr = NULL, *a1 = cpy;
        while ((name = strtok_r (a1, ".", &saveptr))) {
            if (!(o = json_object_get (o, name)))
                log_msg_exit ("`%s' not found in response", objname);
            a1 = NULL;
        }
        free (cpy);
    }

    /* Display the resulting object/value, optionally forcing
     * the type to int or dobule, and optionally scaling the result.
     */
    scale = optparse_get_double (p, "scale", 1.0);
    typestr = optparse_get_str (p, "type", NULL);
    if (json_typeof (o) == JSON_INTEGER || (typestr && !strcmp (typestr, "int"))) {
        double d = json_number_value (o);
        printf ("%d\n", (int)(d * scale));
    } else if (json_typeof (o) == JSON_REAL || (typestr && !strcmp (typestr, "double"))) {
        double d = json_number_value (o);
        printf ("%lf\n", d * scale);
    } else {
        char *s;
        s = json_dumps (o, JSON_INDENT(1) | JSON_ENCODE_ANY);
        printf ("%s\n", s ? s : "Error encoding JSON");
        free (s);
    }

    json_decref (obj);
}

int cmd_stats (optparse_t *p, int argc, char **argv)
{
    int n;
    char *topic = NULL;
    char *service;
    uint32_t nodeid;
    const char *json_str;
    flux_future_t *f = NULL;
    flux_t *h;

    if ((n = optparse_option_index (p)) < argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    service = n < argc ? argv[n++] : "cmb";
    nodeid = optparse_get_int (p, "rank", FLUX_NODEID_ANY);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "clear")) {
        topic = xasprintf ("%s.stats.clear", service);
        if (!(f = flux_rpc (h, topic, NULL, nodeid, 0)))
            log_err_exit ("%s", topic);
        if (flux_future_get (f, NULL) < 0)
            log_err_exit ("%s", topic);
    } else if (optparse_hasopt (p, "clear-all")) {
        topic = xasprintf ("%s.stats.clear", service);
        flux_msg_t *msg = flux_event_encode (topic, NULL);
        if (!msg)
            log_err_exit ("creating event");
        if (flux_send (h, msg, 0) < 0)
            log_err_exit ("sending event");
        flux_msg_destroy (msg);
    } else if (optparse_hasopt (p, "rusage")) {
        topic = xasprintf ("%s.rusage", service);
        if (!(f = flux_rpc (h, topic, NULL, nodeid, 0)))
            log_err_exit ("%s", topic);
        if (flux_rpc_get (f, &json_str) < 0)
            log_err_exit ("%s", topic);
        if (!json_str)
            log_errn_exit (EPROTO, "%s", topic);
        parse_json (p, json_str);
    } else {
        topic = xasprintf ("%s.stats.get", service);
        if (!(f = flux_rpc (h, topic, NULL, nodeid, 0)))
            log_err_exit ("%s", topic);
        if (flux_rpc_get (f, &json_str) < 0)
            log_err_exit ("%s", topic);
        if (!json_str)
            log_errn_exit (EPROTO, "%s", topic);
        parse_json (p, json_str);
    }
    free (topic);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_debug (optparse_t *p, int argc, char **argv)
{
    int n;
    flux_t *h;
    char *topic = NULL;
    const char *op = "setbit";
    int flags = 0;
    flux_future_t *f = NULL;

    if ((n = optparse_option_index (p)) != argc - 1)
        log_msg_exit ("flux-debug requires service argument");
    topic = xasprintf ("%s.debug", argv[n]);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "clear")) {
        op = "clr";
    }
    else if (optparse_hasopt (p, "set")) {
        op = "set";
        flags = strtoul (optparse_get_str (p, "set", NULL), NULL, 0);
    }
    else if (optparse_hasopt (p, "clearbit")) {
        op = "clrbit";
        flags = strtoul (optparse_get_str (p, "clearbit", NULL), NULL, 0);
    }
    else if (optparse_hasopt (p, "setbit")) {
        op = "setbit";
        flags = strtoul (optparse_get_str (p, "setbit", NULL), NULL, 0);
    }
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0, "{s:s s:i}",
                             "op", op, "flags", flags)))
        log_err_exit ("%s", topic);
    if (flux_rpc_get_unpack (f, "{s:i}", "flags", &flags) < 0)
        log_err_exit ("%s", topic);
    printf ("0x%x\n", flags);
    flux_future_destroy (f);
    flux_close (h);
    free (topic);
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
