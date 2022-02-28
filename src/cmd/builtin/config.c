/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <jansson.h>

#include "src/common/libutil/jpath.h"
#include "src/common/libutil/fsd.h"

#include "builtin.h"

typedef enum {
    FSD_NONE,
    FSD_INTEGER,
    FSD_REAL,
    FSD_STRING,
} fsd_subtype_t;

struct map {
    const char *s;
    json_type type;
    fsd_subtype_t fsd_subtype;
};

static struct map typemap[] = {
    { "object", JSON_OBJECT, FSD_NONE },
    { "array", JSON_ARRAY, FSD_NONE },
    { "string", JSON_STRING, FSD_NONE },
    { "integer", JSON_INTEGER, FSD_NONE },
    { "real", JSON_REAL, FSD_NONE },
    { "boolean", JSON_TRUE, FSD_NONE }, // special case
    { "any", JSON_NULL, FSD_NONE },     // special case
    { "fsd", JSON_STRING, FSD_STRING },
    { "fsd-integer", JSON_STRING, FSD_INTEGER },
    { "fsd-real", JSON_STRING, FSD_REAL },
};

static int parse_json_type (const char *s,
                            json_type *type,
                            fsd_subtype_t *fsd_subtype)
{
    for (int i = 0; i < sizeof (typemap) / sizeof (typemap[0]); i++) {
        if (!strcasecmp (s, typemap[i].s)) {
            *type = typemap[i].type;
            *fsd_subtype = typemap[i].fsd_subtype;
            return 0;
        }
    }
    return -1;
}

static void print_object (json_t *o)
{
    if (json_is_string (o))
        printf ("%s\n", json_string_value (o));
    else {
        char *s;
        if (!(s = json_dumps (o, JSON_COMPACT | JSON_ENCODE_ANY)))
            log_msg_exit ("error encoding json object");
        printf ("%s\n", s);
        free (s);
    }
}

static void print_config_item (json_t *o,
                               const char *path,
                               json_type want_type,
                               fsd_subtype_t fsd_subtype,
                               optparse_t *p)
{
    json_type type;
    double t;

    if (path) {
        if (!(o = jpath_get (o, path))) {
            if (errno == ENOENT) {
                if (optparse_hasopt (p, "default")) {
                    printf ("%s\n", optparse_get_str (p, "default", NULL));
                    return;
                }
                if (optparse_hasopt (p, "quiet"))
                    exit (1);
                log_msg_exit ("%s is not set", path);
            }
            log_err_exit ("%s", path);
        }
    }
    type = json_typeof (o);
    if (want_type == JSON_NULL // any
        || (want_type == JSON_TRUE && type == JSON_FALSE) // boolean
        || (want_type == type && fsd_subtype == FSD_NONE)) {
        print_object (o);
    }
    else if (want_type == type
        && fsd_subtype != FSD_NONE
        && fsd_parse_duration (json_string_value (o), &t) == 0) {
        if (fsd_subtype == FSD_INTEGER)
            printf ("%d\n", (int)t);
        else if (fsd_subtype == FSD_REAL)
            printf ("%f\n", t);
        else
            printf ("%s\n", json_string_value (o));
    }
    else
        log_msg_exit ("%s does not have the requested type",
                      path ? path : "value");
}

static int config_get (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f = NULL;
    json_t *o;
    const char *path = NULL;
    const char *typestr;
    json_type type;
    fsd_subtype_t fsd_subtype;

    if (optindex < ac)
        path = av[optindex++];
    if (ac - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "o", &o) < 0)
        log_msg_exit ("Error fetching config object: %s",
                      future_strerror (f, errno));
    typestr = optparse_get_str (p, "type", "any");
    if (parse_json_type (typestr, &type, &fsd_subtype) < 0)
        log_msg_exit ("Unknown type: %s", typestr);

    print_config_item (o,
                       path,
                       type,
                       fsd_subtype,
                       p);

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int builtin_get (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    const char *name;
    const char *value;

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    name = av[optindex];
    value = flux_conf_builtin_get (name, FLUX_CONF_AUTO);
    if (!value)
        log_msg_exit ("%s is invalid", name);
    printf ("%s\n", value);
    return (0);
}

static int internal_config_reload (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f = NULL;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "config.reload", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("error constructing config.reload RPC");
    if (flux_future_get (f, NULL) < 0)
        log_msg_exit ("reload: %s", flux_future_error_string (f));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_config (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-config");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option get_opts[] = {
    { .name = "type", .key = 't', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Set expected type (any, string, integer, real, boolean"
          ", object, array, fsd, fsd-integer, fsd-real)",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Suppress printing of \"[key] is not set\" errors."
    },
    { .name = "default", .key = 'd', .has_arg = 1, .arginfo = "VAL",
      .usage = "Use this value if config key is unset"
    },
    OPTPARSE_TABLE_END
};


static struct optparse_subcommand config_subcmds[] = {
    { "reload",
      "[OPTIONS]",
      "Reload broker configuration from files",
      internal_config_reload,
      0,
      NULL,
    },
    { "get",
      "[--type=TYPE] [--quiet] [--default=VAL] [PATH]",
      "Query broker configuration values",
      config_get,
      0,
      get_opts,
    },
    { "builtin",
      "NAME",
      "Print compiled-in Flux configuration values",
      builtin_get,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_config_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "config", cmd_config, NULL, "Manage configuration", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "config"),
                                  config_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
