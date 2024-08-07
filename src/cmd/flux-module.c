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
#include <flux/core.h>
#include <flux/optparse.h>
#include <jansson.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <assert.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"

const int max_idle = 99;

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_remove (optparse_t *p, int argc, char **argv);
int cmd_load (optparse_t *p, int argc, char **argv);
int cmd_reload (optparse_t *p, int argc, char **argv);
int cmd_stats (optparse_t *p, int argc, char **argv);
int cmd_debug (optparse_t *p, int argc, char **argv);

static struct optparse_option list_opts[] = {
    { .name = "long",  .key = 'l',  .has_arg = 0,
      .usage = "Include full DSO path for each module", },
    OPTPARSE_TABLE_END,
};

static struct optparse_option remove_opts[] =  {
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Ignore nonexistent modules",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_option reload_opts[] =  {
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Ignore nonexistent modules",
    },
    { .name = "name", .has_arg = 1, .arginfo = "NAME",
      .usage = "Override default module name",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_option load_opts[] =  {
    { .name = "name", .has_arg = 1, .arginfo = "NAME",
      .usage = "Override default module name",
    },
    OPTPARSE_TABLE_END,
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
      "[OPTIONS]",
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
    { "unload",
      "[OPTIONS] module",
      "Unload module",
      cmd_remove,
      OPTPARSE_SUBCMD_HIDDEN,
      remove_opts,
    },
    { "load",
      "[OPTIONS] module",
      "Load module",
      cmd_load,
      0,
      load_opts,
    },
    { "reload",
      "[OPTIONS] module",
      "Reload module",
      cmd_reload,
      0,
      reload_opts,
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
        if (!(s->flags & OPTPARSE_SUBCMD_HIDDEN))
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

static bool has_suffix (const char *s, const char *suffix)
{
    int n = strlen (s) - strlen (suffix);
    if (n >= 0 && streq (&s[n], suffix))
        return true;
    return false;
}

/* If path looks like a dso filename, then canonicalize it so that
 * the broker, possibly running in a different directory, can dlopen() it.
 */
static int canonicalize_if_path (const char *path, char **fullpathp)
{
    char *fullpath = NULL;
    if (strchr (path, '/') || has_suffix (path, ".so")) {
        if (!(fullpath = realpath (path, NULL)))
            return -1;
    }
    *fullpathp = fullpath;
    return 0;
}

static json_t *args_create (int argc, char **argv)
{
    json_t *args;
    if (!(args = json_array ()))
        goto error;
    for (int i = 0; i < argc; i++) {
        json_t *s = json_string (argv[i]);
        if (!s || json_array_append_new (args, s) < 0) {
            json_decref (s);
            goto error;
        }
    }
    return args;
error:
    json_decref (args);
    return NULL;
}

static int set_string (json_t *o, const char *key, const char *val)
{
    json_t *s = json_string (val);
    if (!s || json_object_set_new (o, key, s) < 0) {
        json_decref (s);
        return -1;
    }
    return 0;
}

static void module_load (flux_t *h,
                         optparse_t *p,
                         const char *path,
                         int argc,
                         char **argv)
{
    const char *name = optparse_get_str (p, "name", NULL);
    char *fullpath = NULL;
    flux_future_t *f;
    json_t *args;
    json_t *payload;

    if (canonicalize_if_path (path, &fullpath) < 0)
        log_err_exit ("could not canonicalize module path '%s'", path);
    if (!(args = args_create (argc, argv))
        || !(payload = json_pack ("{s:s s:O}",
                               "path", fullpath ? fullpath : path,
                               "args", args))
        || (name && set_string (payload, "name", name) < 0))
        log_msg_exit ("failed to create module.load payload");
    if (!(f = flux_rpc_pack (h,
                             "module.load",
                             FLUX_NODEID_ANY,
                             0,
                             "O",
                             payload))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg_exit ("load %s: %s", path, future_strerror (f, errno));
    }
    flux_future_destroy (f);
    json_decref (payload);
    json_decref (args);
    free (fullpath);
}

int cmd_load (optparse_t *p, int argc, char **argv)
{
    int n = optparse_option_index (p);
    const char *path;
    flux_t *h;

    if (n == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    path = argv[n++];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    module_load (h, p, path, argc - n, argv + n);

    flux_close (h);
    return 0;
}

static void module_remove (flux_t *h, optparse_t *p, const char *path)
{
    char *fullpath = NULL;
    flux_future_t *f;

    if (canonicalize_if_path (path, &fullpath) < 0)
        log_err_exit ("could not canonicalize module path '%s'", path);

    if (!(f = flux_rpc_pack (h,
                             "module.remove",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name", fullpath ? fullpath : path))
        || flux_rpc_get (f, NULL) < 0) {
        if (!(optparse_hasopt (p, "force") && errno == ENOENT))
            log_msg_exit ("remove %s: %s", path, future_strerror (f, errno));
    }
    flux_future_destroy (f);
    free (fullpath);
}

int cmd_remove (optparse_t *p, int argc, char **argv)
{
    int n = optparse_option_index (p);
    char *path;
    flux_t *h;

    if (n != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    path = argv[n];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    module_remove (h, p, path);

    flux_close (h);
    return (0);
}

int cmd_reload (optparse_t *p, int argc, char **argv)
{
    int n = optparse_option_index (p);
    const char *name = optparse_get_str (p, "name", NULL);
    const char *path;
    flux_t *h;

    if (n == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    path = argv[n++];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* If --name=NAME was specified, remove by that name rather than
     * the path so the correct instantiation of the DSO is selected.
     */
    module_remove (h, p, name ? name : path);

    module_load (h, p, path, argc - n, argv + n);
    return (0);
}

/* Translate an array of service names to a comma-delimited string.
 * If list is empty, NULL is returned.
 * If skip != NULL, skip over that name in the list (intended to be
 * the module name, implicitly registered as a service).
 * Caller must free result.
 */
char *lsmod_services_string (json_t *services, const char *skip, int maxcol)
{
    size_t index;
    json_t *value;
    char *argz = NULL;
    size_t argz_len = 0;

    json_array_foreach (services, index, value) {
        const char *name = json_string_value (value);
        if (name && (!skip || !streq (name, skip)))
            if (argz_add (&argz, &argz_len, name) != 0)
                oom ();
    }
    if (argz)
        argz_stringify (argz, argz_len, ',');
    if (argz && maxcol > 0 && strlen (argz) > maxcol) {
        argz[maxcol - 1] = '+';
        argz[maxcol] = '\0';
    }
    return argz;
}

void lsmod_idle_string (int idle, char *buf, int bufsz)
{
    if (idle <= max_idle)
        snprintf (buf, bufsz, "%d", idle);
    else
        snprintf (buf, bufsz, "idle");
}

char lsmod_state_char (int state)
{
    switch (state) {
    case FLUX_MODSTATE_INIT:
        return 'I';
    case FLUX_MODSTATE_RUNNING:
        return 'R';
    case FLUX_MODSTATE_FINALIZING:
        return 'F';
    case FLUX_MODSTATE_EXITED:
        return 'X';
    default:
        return '?';
    }
}

void lsmod_print_header (FILE *f, bool longopt)
{
    if (longopt) {
        fprintf (f,
                 "%-24.24s %4s  %c %s %s %-8s %s\n",
                 "Module", "Idle", 'S', "Sendq", "Recvq", "Service", "Path");
    }
    else {
        fprintf (f,
                 "%-24s %4s  %c %s %s %s\n",
                 "Module", "Idle", 'S', "Sendq", "Recvq", "Service");
    }
}

void lsmod_print_entry (FILE *f,
                       const char *name,
                       const char *path,
                       int idle,
                       int status,
                       json_t *services,
                       int sendqueue,
                       int recvqueue,
                       bool longopt)
{
    char idle_s[16];
    char state = lsmod_state_char (status);

    lsmod_idle_string (idle, idle_s, sizeof (idle_s));

    if (longopt) {
        char *s = lsmod_services_string (services, name, 8);
        fprintf (f, "%-24.24s %4s  %c %5d %5d %-8s %s\n",
                 name,
                 idle_s,
                 state,
                 sendqueue,
                 recvqueue,
                 s ? s : "",
                 path);
        free (s);
    }
    else {
        char *s = lsmod_services_string (services, name, 0);
        fprintf (f, "%-24.24s %4s  %c %5d %5d %s\n",
                 name,
                 idle_s,
                 state,
                 sendqueue,
                 recvqueue,
                 s ? s : "");
        free (s);
    }
}

void lsmod_print_list (FILE *f, json_t *o, bool longopt)
{
    size_t index;
    json_t *value;
    const char *name;
    const char *path;
    int idle;
    int status;
    json_t *services;
    int recvqueue;
    int sendqueue;

    json_array_foreach (o, index, value) {
        recvqueue = 0;
        sendqueue = 0;
        if (json_unpack (value, "{s:s s:s s:i s:i s:o s?i s?i}",
                         "name", &name,
                         "path", &path,
                         "idle", &idle,
                         "status", &status,
                         "services", &services,
                         "recvqueue", &recvqueue,
                         "sendqueue", &sendqueue) < 0)
            log_msg_exit ("Error parsing lsmod response");
        if (!json_is_array (services))
            log_msg_exit ("Error parsing lsmod services array");
        lsmod_print_entry (f,
                           name,
                           path,
                           idle,
                           status,
                           services,
                           sendqueue,
                           recvqueue,
                           longopt);
    }
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    bool longopt = optparse_hasopt (p, "long");
    flux_future_t *f;
    flux_t *h;
    int n;
    json_t *o;

    if ((n = optparse_option_index (p)) < argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc (h, "module.list", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "{s:o}", "mods", &o) < 0)
        log_err_exit ("list");
    lsmod_print_header (stdout, longopt);
    lsmod_print_list (stdout, o, longopt);
    flux_future_destroy (f);
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
    if ((objname = optparse_get_str (p, "parse", NULL))) {
        if (!(o = jpath_get (obj, objname)))
            log_msg_exit ("`%s' not found in response", objname);
    }
    else
        o = obj;

    /* Display the resulting object/value, optionally forcing
     * the type to int or double, and optionally scaling the result.
     */
    scale = optparse_get_double (p, "scale", 1.0);
    typestr = optparse_get_str (p, "type", NULL);
    if (json_typeof (o) == JSON_INTEGER
        || (typestr && streq (typestr, "int"))) {
        double d = json_number_value (o);
        printf ("%d\n", (int)(d * scale));
    } else if (json_typeof (o) == JSON_REAL
        || (typestr && streq (typestr, "double"))) {
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
    service = n < argc ? argv[n++] : "broker";
    nodeid = FLUX_NODEID_ANY;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "clear")) {
        topic = xasprintf ("%s.stats-clear", service);
        if (!(f = flux_rpc (h, topic, NULL, nodeid, 0)))
            log_err_exit ("%s", topic);
        if (flux_future_get (f, NULL) < 0)
            log_err_exit ("%s", topic);
    } else if (optparse_hasopt (p, "clear-all")) {
        topic = xasprintf ("%s.stats-clear", service);
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
        topic = xasprintf ("%s.stats-get", service);
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
