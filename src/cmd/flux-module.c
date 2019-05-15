/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include <argz.h>
#include <assert.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/iterators.h"

const int max_idle = 99;

typedef int(flux_lsmod_f) (const char *name,
                           int size,
                           const char *digest,
                           int idle,
                           int status,
                           const char *nodeset,
                           const char *services,
                           void *arg);

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_remove (optparse_t *p, int argc, char **argv);
int cmd_load (optparse_t *p, int argc, char **argv);
int cmd_info (optparse_t *p, int argc, char **argv);
int cmd_stats (optparse_t *p, int argc, char **argv);
int cmd_debug (optparse_t *p, int argc, char **argv);

#define RANK_OPTION                                                     \
    {                                                                   \
        .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "NODESET", \
        .usage = "Include NODESET in operation target",                 \
    }
#define EXCLUDE_OPTION                                                     \
    {                                                                      \
        .name = "exclude", .key = 'x', .has_arg = 1, .arginfo = "NODESET", \
        .usage = "Exclude NODESET from operation target",                  \
    }

static struct optparse_option list_opts[] = {RANK_OPTION,
                                             EXCLUDE_OPTION,
                                             OPTPARSE_TABLE_END};
static struct optparse_option remove_opts[] = {RANK_OPTION,
                                               EXCLUDE_OPTION,
                                               OPTPARSE_TABLE_END};
static struct optparse_option load_opts[] = {RANK_OPTION,
                                             EXCLUDE_OPTION,
                                             OPTPARSE_TABLE_END};
static struct optparse_option stats_opts[] = {{
                                                  .name = "parse",
                                                  .key = 'p',
                                                  .has_arg = 1,
                                                  .arginfo = "OBJNAME",
                                                  .usage = "Parse object "
                                                           "period-delimited object "
                                                           "name",
                                              },
                                              {
                                                  .name = "scale",
                                                  .key = 's',
                                                  .has_arg = 1,
                                                  .arginfo = "N",
                                                  .usage = "Scale numeric JSON value "
                                                           "by N",
                                              },
                                              {
                                                  .name = "type",
                                                  .key = 't',
                                                  .has_arg = 1,
                                                  .arginfo = "int|double",
                                                  .usage = "Convert JSON value to "
                                                           "specified type",
                                              },
                                              {
                                                  .name = "rank",
                                                  .key = 'r',
                                                  .has_arg = 1,
                                                  .arginfo = "RANK",
                                                  .usage = "Target specified rank",
                                              },
                                              {
                                                  .name = "rusage",
                                                  .key = 'R',
                                                  .has_arg = 0,
                                                  .usage = "Request rusage data "
                                                           "instead of stats",
                                              },
                                              {
                                                  .name = "clear",
                                                  .key = 'c',
                                                  .has_arg = 0,
                                                  .usage = "Clear stats on target rank",
                                              },
                                              {
                                                  .name = "clear-all",
                                                  .key = 'C',
                                                  .has_arg = 0,
                                                  .usage = "Clear stats on all ranks",
                                              },
                                              OPTPARSE_TABLE_END};
static struct optparse_option debug_opts[] = {
    {
        .name = "clear",
        .key = 'C',
        .has_arg = 0,
        .usage = "Set debug flags to 0",
    },
    {
        .name = "set",
        .key = 'S',
        .has_arg = 1,
        .usage = "Set debug flags to MASK",
    },
    {
        .name = "setbit",
        .key = 's',
        .has_arg = 1,
        .usage = "Set one debug flag to 1",
    },
    {
        .name = "clearbit",
        .key = 'c',
        .has_arg = 1,
        .usage = "Set one debug flag to 0",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand subcommands[] =
    {{
         "list",
         "[OPTIONS] [module]",
         "List loaded modules",
         cmd_list,
         0,
         list_opts,
     },
     {
         "remove",
         "[OPTIONS] module",
         "Unload module",
         cmd_remove,
         0,
         remove_opts,
     },
     {
         "load",
         "[OPTIONS] module",
         "Load module",
         cmd_load,
         0,
         load_opts,
     },
     {"info", "[OPTIONS] module", "Display module info", cmd_info, 0, NULL},
     {
         "stats",
         "[OPTIONS] module",
         "Display stats on module",
         cmd_stats,
         0,
         stats_opts,
     },
     {
         "debug",
         "[OPTIONS] module",
         "Get/set module debug flags",
         cmd_debug,
         0,
         debug_opts,
     },
     OPTPARSE_SUBCMD_END};

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

void module_dlerror (const char *errmsg, void *arg)
{
    log_msg ("%s", errmsg);
}

void parse_modarg (const char *arg, char **name, char **path)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (strchr (arg, '/')) {
        if (!(modpath = realpath (arg, NULL)))
            log_err_exit ("%s", arg);
        if (!(modname = flux_modname (modpath, module_dlerror, NULL)))
            log_msg_exit ("%s", modpath);
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            log_msg_exit ("FLUX_MODULE_PATH is not set");
        modname = xstrdup (arg);
        if (!(modpath = flux_modfind (searchpath, modname, module_dlerror, NULL)))
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

static struct idset *ns_create_special (flux_t *h, const char *arg)
{
    struct idset *ns = NULL;

    if (!arg) {
        uint32_t rank;
        if (flux_get_rank (h, &rank) < 0)
            log_err_exit ("flux_get_rank");
        if (!(ns = idset_create (0, IDSET_FLAG_AUTOGROW)))
            log_err_exit ("idset_create");
        if (idset_set (ns, rank) < 0)
            log_err_exit ("idset_set");
    } else if (!strcmp (arg, "all")) {
        uint32_t size;
        if (flux_get_size (h, &size) < 0)
            log_err_exit ("flux_get_size");
        if (!(ns = idset_create (0, IDSET_FLAG_AUTOGROW)))
            log_err_exit ("idset_create");
        if (idset_range_set (ns, 0, size - 1) < 0)
            log_err_exit ("idset_range_set");
    } else {
        if (!(ns = idset_decode (arg)))
            return NULL;
    }
    return ns;
}

/* Parse --rank and --exclude options, returning a nodeset.
 * If neither are specified, default to the local rank.
 * Exit with error if an option argument is malformed (or other error).
 * Return value will never be NULL, but it may be an empty nodeset.
 */
struct idset *parse_nodeset_options (optparse_t *p, flux_t *h)
{
    const char *nodeset = optparse_get_str (p, "rank", NULL);
    const char *exclude = optparse_get_str (p, "exclude", NULL);
    struct idset *ns;

    if (!(ns = ns_create_special (h, nodeset)))
        log_msg_exit ("could not parse --rank option");
    if (exclude) {
        struct idset *xns;
        uint32_t rank;
        if (!(xns = ns_create_special (h, exclude)))
            log_msg_exit ("could not parse --exclude option");
        rank = idset_first (xns);
        while (rank != IDSET_INVALID_ID) {
            if (idset_clear (ns, rank) < 0)
                log_err_exit ("idset_clear");
            rank = idset_next (xns, rank);
        }
        idset_destroy (xns);
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
    struct idset *ns;
    flux_mrpc_t *r = NULL;
    char *nodeset = NULL;

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
    if (idset_count (ns) == 0)
        goto done;
    if (!(nodeset = idset_encode (ns, IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS)))
        log_err_exit ("idset_encode");
    if (!(r = flux_mrpc_pack (h,
                              topic,
                              nodeset,
                              0,
                              "{s:s s:O}",
                              "path",
                              modpath,
                              "args",
                              args)))
        log_err_exit ("%s", topic);
    do {
        if (flux_mrpc_get (r, NULL) < 0) {
            uint32_t nodeid = FLUX_NODEID_ANY;
            int saved_errno = errno;
            (void)flux_mrpc_get_nodeid (r, &nodeid);
            errno = saved_errno;
            if (errno == EEXIST && nodeid != FLUX_NODEID_ANY)
                log_msg ("%s[%" PRIu32 "]: %s module/service is in use",
                         topic,
                         nodeid,
                         modname);
            else if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", topic, nodeid);
            else
                log_err ("%s", topic);
            errors++;
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    free (nodeset);
    idset_destroy (ns);
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
    struct idset *ns;
    flux_t *h;
    flux_mrpc_t *r = NULL;
    int n;
    char *nodeset = NULL;

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
    if (idset_count (ns) == 0)
        goto done;
    if (!(nodeset = idset_encode (ns, IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS)))
        log_err_exit ("idset_encode");
    if (!(r = flux_mrpc_pack (h, topic, nodeset, 0, "{s:s}", "name", modname)))
        log_err_exit ("%s %s", topic, modname);
    do {
        if (flux_mrpc_get (r, NULL) < 0) {
            uint32_t nodeid = FLUX_NODEID_ANY;
            int saved_errno = errno;
            (void)flux_mrpc_get_nodeid (r, &nodeid);
            errno = saved_errno;
            log_err ("%s[%d] %s",
                     topic,
                     nodeid == FLUX_NODEID_ANY ? -1 : nodeid,
                     modname);
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    free (nodeset);
    idset_destroy (ns);
    free (topic);
    free (service);
    flux_close (h);
    return (0);
}

int lsmod_print_cb (const char *name,
                    int size,
                    const char *digest,
                    int idle,
                    int status,
                    const char *nodeset,
                    const char *services,
                    void *arg)
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
            S = 'I';
            break;
        case FLUX_MODSTATE_SLEEPING:
            S = 'S';
            break;
        case FLUX_MODSTATE_RUNNING:
            S = 'R';
            break;
        case FLUX_MODSTATE_FINALIZING:
            S = 'F';
            break;
        case FLUX_MODSTATE_EXITED:
            S = 'X';
            break;
        default:
            S = '?';
            break;
    }
    printf ("%-20.20s %8d %7s %4s  %c  %8s %s\n",
            name,
            size,
            digest_len > 7 ? digest + digest_len - 7 : digest,
            idle_str,
            S,
            nodeset ? nodeset : "",
            services ? services : "");
    return 0;
}

struct module_record {
    char *name;
    int size;
    char *digest;
    int idle;
    int status_hist[8];
    struct idset *nodeset;
    char *services;
};

/* N.B. this function matches the zhashx destructor prototype.  Avoid casting
 * with zhashx typedefs as they are unavailable in older czmq releases.
 */
void module_record_destroy (void **item)
{
    if (item && *item != NULL) {
        int saved_errno = errno;
        struct module_record *m = *item;
        free (m->name);
        free (m->digest);
        idset_destroy (m->nodeset);
        free (m->services);
        free (m);
        *item = NULL;
        errno = saved_errno;
    }
}

struct module_record *module_record_create (const char *name,
                                            int size,
                                            const char *digest,
                                            int idle,
                                            int status,
                                            uint32_t nodeid,
                                            const char *services)
{
    struct module_record *m = xzmalloc (sizeof (*m));
    m->name = xstrdup (name);
    m->size = size;
    m->digest = xstrdup (digest);
    m->idle = idle;
    m->services = services ? xstrdup (services) : NULL;
    m->status_hist[abs (status) % 8]++;
    if (!(m->nodeset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        log_err_exit ("idset_create");
    if (idset_set (m->nodeset, nodeid) < 0)
        log_err_exit ("idset_set");
    return m;
}

/* insert_service_list() creates a sorted zlistx_t on demand and inserts
 * 'name' into it.  The zlistx_t is only used as an intermediate sorting
 * step during conversion of json array to argz.  The list must be sorted
 * because it is combined with the module digest to make a hash key, and
 * we need exactly one hash entry (line of lsmod output) for each unique
 * module hash + service set.
 */
int service_cmp (const void *item1, const void *item2)
{
    return strcmp (item1, item2);
}
void *service_dup (const void *item)
{
    return strdup (item);
}
void service_destroy (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}
void insert_service_list (zlistx_t **l, const char *name)
{
    if (!*l) {
        if (!(*l = zlistx_new ()))
            oom ();
        zlistx_set_comparator (*l, service_cmp);
        zlistx_set_duplicator (*l, service_dup);
        zlistx_set_destructor (*l, service_destroy);
    }
    if (!zlistx_insert (*l, (void *)name, true))
        oom ();
}

/* Translate an array of service names to a sorted, concatenated,
 * comma-delimited string.  If list is empty, NULL is returned.
 * If skip != NULL, skip over that name in the list (intended to be
 * the module name, implicitly registered as a service).
 * Caller must free result.
 */
char *lsmod_services_string (json_t *services, const char *skip)
{
    size_t index;
    json_t *value;
    zlistx_t *l = NULL;
    char *argz = NULL;
    size_t argz_len = 0;

    json_array_foreach (services, index, value)
    {
        const char *name = json_string_value (value);
        if (name && (!skip || strcmp (name, skip) != 0))
            insert_service_list (&l, name);
    }
    if (l) {
        char *name;
        FOREACH_ZLISTX (l, name)
        {
            if (argz_add (&argz, &argz_len, name) != 0)
                oom ();
        }
        zlistx_destroy (&l);
        argz_stringify (argz, argz_len, ',');
    }
    return argz;
}

void lsmod_map_hash (zhashx_t *mods, flux_lsmod_f cb, void *arg)
{
    const char *key;
    struct module_record *m;
    int status;
    char *nodeset;

    FOREACH_ZHASHX (mods, key, m)
    {
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
        if (!(nodeset =
                  idset_encode (m->nodeset, IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS)))
            log_err_exit ("idset_encode");
        cb (m->name, m->size, m->digest, m->idle, status, nodeset, m->services, arg);
        free (nodeset);
    }
}

int lsmod_merge_result (uint32_t nodeid, json_t *o, zhashx_t *mods)
{
    struct module_record *m;
    size_t index;
    json_t *value;
    const char *name, *digest;
    int size, idle;
    int status;
    json_t *services;

    if (!json_is_array (o))
        goto proto;
    json_array_foreach (o, index, value)
    {
        if (json_unpack (value,
                         "{s:s s:i s:s s:i s:i s:o}",
                         "name",
                         &name,
                         "size",
                         &size,
                         "digest",
                         &digest,
                         "idle",
                         &idle,
                         "status",
                         &status,
                         "services",
                         &services)
            < 0)
            goto proto;
        if (!json_is_array (services))
            goto proto;

        char *svcstr = lsmod_services_string (services, name);
        char *hash_key = xasprintf ("%s:%s", digest, svcstr ? svcstr : "");
        if ((m = zhashx_lookup (mods, hash_key))) {
            if (idle < m->idle)
                m->idle = idle;
            m->status_hist[abs (status) % 8]++;
            if (idset_set (m->nodeset, nodeid) < 0)
                log_err_exit ("idset_set");
        } else {
            m = module_record_create (name, size, digest, idle, status, nodeid, svcstr);
            zhashx_update (mods, hash_key, m);
        }
        free (hash_key);
        free (svcstr);
    }
    return 0;
proto:
    errno = EPROTO;
    return -1;
}

/* Fetch lsmod data from one or more ranks.  Each lsmod response returns
 * an array of module records.  The records are merged to produce a hash
 * by module SHA1 digest (computed over module file) + services list.
 * Each hash entry is then displayed as a line of output.
 */
int cmd_list (optparse_t *p, int argc, char **argv)
{
    char *service = "cmb";
    char *topic = NULL;
    struct idset *ns;
    flux_mrpc_t *r = NULL;
    flux_t *h;
    int n;
    zhashx_t *mods;
    char *nodeset = NULL;

    if (!(mods = zhashx_new ()))
        oom ();
    zhashx_set_destructor (mods, module_record_destroy);
    if ((n = optparse_option_index (p)) < argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (n < argc)
        service = argv[n++];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    ns = parse_nodeset_options (p, h);
    if (idset_count (ns) == 0)
        goto done;
    if (!(nodeset = idset_encode (ns, IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS)))
        log_err_exit ("idset_encode");

    printf ("%-20s %8s %-7s %4s  %c  %8s %s\n",
            "Module",
            "Size",
            "Digest",
            "Idle",
            'S',
            "Nodeset",
            "Service");
    topic = xasprintf ("%s.lsmod", service);
    if (!(r = flux_mrpc (h, topic, NULL, nodeset, 0)))
        log_err_exit ("%s", topic);
    do {
        json_t *o;
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_mrpc_get_nodeid (r, &nodeid) < 0
            || flux_mrpc_get_unpack (r, "{s:o}", "mods", &o) < 0
            || lsmod_merge_result (nodeid, o, mods) < 0) {
            if (nodeid != FLUX_NODEID_ANY)
                log_err ("%s[%" PRIu32 "]", topic, nodeid);
            else
                log_err ("%s", topic);
        }
    } while (flux_mrpc_next (r) == 0);
done:
    flux_mrpc_destroy (r);
    free (nodeset);
    idset_destroy (ns);
    lsmod_map_hash (mods, lsmod_print_cb, NULL);
    zhashx_destroy (&mods);
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
    } else if (json_typeof (o) == JSON_REAL
               || (typestr && !strcmp (typestr, "double"))) {
        double d = json_number_value (o);
        printf ("%lf\n", d * scale);
    } else {
        char *s;
        s = json_dumps (o, JSON_INDENT (1) | JSON_ENCODE_ANY);
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
    } else if (optparse_hasopt (p, "set")) {
        op = "set";
        flags = strtoul (optparse_get_str (p, "set", NULL), NULL, 0);
    } else if (optparse_hasopt (p, "clearbit")) {
        op = "clrbit";
        flags = strtoul (optparse_get_str (p, "clearbit", NULL), NULL, 0);
    } else if (optparse_hasopt (p, "setbit")) {
        op = "setbit";
        flags = strtoul (optparse_get_str (p, "setbit", NULL), NULL, 0);
    }
    if (!(f = flux_rpc_pack (h,
                             topic,
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i}",
                             "op",
                             op,
                             "flags",
                             flags)))
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
