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
#include <unistd.h>
#include <jansson.h>

#include "src/common/libutil/jpath.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/tomltk.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

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
    for (int i = 0; i < ARRAY_SIZE (typemap); i++) {
        if (!strcasecmp (s, typemap[i].s)) {
            *type = typemap[i].type;
            *fsd_subtype = typemap[i].fsd_subtype;
            return 0;
        }
    }
    return -1;
}

static json_t *parse_boolean (const char *s)
{
    if (s[0] == '0' || s[0] == 'f' || s[0] == 'F' || s[0] == '\0')
        return json_false ();
    return json_true ();
}

static json_t *parse_real (const char *s)
{
    char *endptr;
    errno = 0;
    double d = strtod (s, &endptr);
    if (errno != 0 || *endptr != '\0')
        return NULL;
    return json_real (d);
}

static json_t *parse_int (const char *s)
{
    char *endptr;
    errno = 0;
    json_int_t i = strtoll (s, &endptr, 0);
    if (errno != 0 || *endptr != '\0')
        return NULL;
    return json_integer (i);
}

static json_t *create_json (const char *s,
                            json_type type,
                            fsd_subtype_t fsd_subtype)
{
    json_t *o = NULL;

    switch (type) {
        case JSON_TRUE:
        case JSON_FALSE:
            o = parse_boolean (s);
            break;
        case JSON_REAL:
            if (!(o = parse_real (s)))
                log_msg_exit ("Error parsing real value");
            break;
        case JSON_INTEGER:
            if (!(o = parse_int (s)))
                log_msg_exit ("Error parsing integer value");
            break;
        case JSON_OBJECT:
            if (!(o = json_loads (s, 0, NULL)) || !json_is_object (o))
                log_msg_exit ("Error parsing json object");
            break;
        case JSON_ARRAY:
            if (!(o = json_loads (s, 0, NULL)) || !json_is_array (o))
                log_msg_exit ("Error parsing json array");
            break;
        case JSON_STRING:
            if (fsd_subtype == FSD_STRING) {
                double t;
                if (fsd_parse_duration (s, &t) < 0)
                    log_msg_exit ("Error parsing Flux Standard Duration");
            }
            if (s[0] == '"')
                o = json_loads (s, JSON_DECODE_ANY, NULL);
            else
                o = json_string (s);
            if (!o || !json_is_string (o))
                log_msg_exit ("Error parsing string");
            break;
        case JSON_NULL:
            break; // can't happen
    }
    return o;
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
    flux_t *h = NULL;
    flux_future_t *f = NULL;
    const char *config_path = NULL;
    flux_conf_t *conf = NULL;
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
    if ((config_path = optparse_get_str (p, "config-path", NULL))) {
        char buf[1024];
        flux_error_t error;

        if (streq (config_path, "system")
            || streq (config_path, "security")
            || streq (config_path, "imp")) {
            snprintf (buf,
                      sizeof (buf),
                      "%s/%s/conf.d",
                      FLUXCONFDIR,
                      config_path);
            config_path = buf;
        }
        if (!(conf = flux_conf_parse (config_path, &error))
            || flux_conf_unpack (conf, &error, "o", &o) < 0)
            log_msg_exit ("%s", error.text);
    }
    else {
        if (!(h = builtin_get_flux_handle (p)))
            log_err_exit ("flux_open");
        if (!(f = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
            || flux_rpc_get_unpack (f, "o", &o) < 0)
            log_msg_exit ("Error fetching config object: %s",
                          future_strerror (f, errno));
    }
    typestr = optparse_get_str (p, "type", "any");
    if (parse_json_type (typestr, &type, &fsd_subtype) < 0)
        log_msg_exit ("Unknown type: %s", typestr);

    print_config_item (o,
                       path,
                       type,
                       fsd_subtype,
                       p);

    flux_conf_decref (conf);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int config_unset (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    const char *path;
    flux_future_t *fread;
    flux_future_t *fwrite;
    json_t *o;

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    path = av[optindex++];
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(fread = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (fread, "o", &o) < 0) {
        log_msg_exit ("Error fetching config object: %s",
                      future_strerror (fread, errno));
    }
    if (jpath_del (o, path) < 0)
        log_err_exit ("Error deleting %s from config object", path);
    if (!(fwrite = flux_rpc_pack (h, "config.load", FLUX_NODEID_ANY, 0, "O", o))
        || flux_rpc_get (fwrite, NULL) < 0) {
        log_msg_exit ("Error updating config object: %s",
                      future_strerror (fwrite, errno));
    }
    flux_future_destroy (fwrite);
    flux_future_destroy (fread);
    flux_close (h);
    return 0;
}

static int config_set (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    const char *typestr = optparse_get_str (p, "type", NULL);
    json_type type = JSON_STRING;
    fsd_subtype_t fsd_subtype = FSD_NONE;
    flux_t *h;
    const char *path;
    const char *value;
    flux_future_t *fread;
    flux_future_t *fwrite;
    json_t *o;
    json_t *old_val;
    json_t *new_val;

    if (optindex != ac - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    path = av[optindex++];
    value = av[optindex++];
    if (typestr) {
        if (parse_json_type (typestr, &type, &fsd_subtype) < 0)
            log_msg_exit ("Unknown type: %s", typestr);
        if (streq (typestr, "fsd-integer") || streq (typestr, "fsd-real"))
            log_msg_exit ("Invalid type for the set subcommand: %s", typestr);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(fread = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (fread, "o", &o) < 0) {
        log_msg_exit ("Error fetching config object: %s",
                      future_strerror (fread, errno));
    }
    /* Match the type of the old value, if any (unless overridden).
     * If it's not set, then require --type.
     */
    if (!typestr) {
        if (!(old_val = jpath_get (o, path)))
            log_msg_exit ("Type is unknown, please specify --type");
        type = json_typeof (old_val);
        fsd_subtype = FSD_NONE;
    }
    if (!(new_val = create_json (value, type, fsd_subtype))
        || jpath_set_new (o, path, new_val) < 0)
        log_msg_exit ("Error updating config object");
    if (!(fwrite = flux_rpc_pack (h, "config.load", FLUX_NODEID_ANY, 0, "O", o))
        || flux_rpc_get (fwrite, NULL) < 0) {
        log_msg_exit ("Error updating config object: %s",
                      future_strerror (fwrite, errno));
    }

    flux_future_destroy (fwrite);
    flux_future_destroy (fread);
    flux_close (h);
    return 0;
}

static int builtin_get (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    const char *name;
    const char *value;
    int flags;

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "installed"))
        flags = FLUX_CONF_INSTALLED;
    else if (optparse_hasopt (p, "intree"))
        flags = FLUX_CONF_INTREE;
    else
        flags = FLUX_CONF_AUTO;

    name = av[optindex];
    value = flux_conf_builtin_get (name, flags);
    if (!value)
        log_msg_exit ("%s is invalid", name);
    printf ("%s\n", value);
    return (0);
}

static int config_reload (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f = NULL;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "follower-noop")) {
        uint32_t rank;
        if (flux_get_rank (h, &rank) == 0 && rank > 0)
            goto done;
    }
    if (!(f = flux_rpc (h, "config.reload", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("error constructing config.reload RPC");
    if (flux_future_get (f, NULL) < 0)
        log_msg_exit ("reload: %s", flux_future_error_string (f));
    flux_future_destroy (f);
done:
    flux_close (h);
    return (0);
}

static int config_load (optparse_t *p, int ac, char *av[])
{
    int index = optparse_option_index (p);
    const char *path = NULL;
    json_t *obj;
    flux_t *h;
    flux_future_t *f;

    if (index < ac)
        path = av[index++];
    if (index != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (path) {
        flux_conf_t *conf;
        flux_error_t error;

        if (!(conf = flux_conf_parse (path, &error))
            || flux_conf_unpack (conf, &error, "O", &obj) < 0)
            log_msg_exit ("Error parsing config: %s", error.text);
        flux_conf_decref (conf);
    }
    else {
        ssize_t n;
        void *buf;

        if ((n = read_all (STDIN_FILENO, &buf)) < 0)
            log_err_exit ("error reading stdin");

        if (!(obj = json_loads (buf, 0, NULL))) {
            struct tomltk_error error;
            toml_table_t *tab;

            if (!(tab = tomltk_parse (buf, n, &error)))
                log_msg_exit ("TOML parse error: %s", error.errbuf);
            if (!(obj = tomltk_table_to_json (tab)))
                log_err_exit ("error converting TOML to JSON");
            toml_free (tab);
        }
        free (buf);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h, "config.load", FLUX_NODEID_ANY, 0, "O", obj)))
        log_err_exit ("error constructing config.load RPC");
    if (flux_future_get (f, NULL) < 0)
        log_msg_exit ("load: %s", flux_future_error_string (f));
    flux_future_destroy (f);
    flux_close (h);

    json_decref (obj);
    return 0;
}

int cmd_config (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-config");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option reload_opts[] = {
    { .name = "follower-noop", .has_arg = 0,
      .usage = "Do nothing if run on a non-leader broker (for systemd use)"
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option get_opts[] = {
    { .name = "config-path", .key = 'c', .has_arg = 1,
      .arginfo = "PATH|system|security|imp",
      .usage = "Get broker config from PATH (default: use live config)"
    },
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

static struct optparse_option builtin_opts[] = {
    { .name = "intree", .has_arg = 0,
      .usage = "Force in-tree paths to be used"
    },
    { .name = "installed", .has_arg = 0,
      .usage = "Force installed paths to be used",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option set_opts[] = {
    { .name = "type", .key = 't', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Specify type (string, integer, real, boolean"
          ", object, array, fsd)",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand config_subcmds[] = {
    { "load",
      "[PATH]",
      "Load broker configuration from stdin or PATH",
      config_load,
      0,
      NULL,
    },
    { "reload",
      "[OPTIONS]",
      "Reload broker configuration from files",
      config_reload,
      0,
      reload_opts,
    },
    { "get",
      "[OPTIONS] [NAME]",
      "Query broker configuration values",
      config_get,
      0,
      get_opts,
    },
    { "set",
      "[OPTIONS] NAME VALUE",
      "Set broker configuration value",
      config_set,
      0,
      set_opts,
    },
    { "unset",
      "NAME",
      "Unset broker configuration value",
      config_unset,
      0,
      NULL,
    },
    { "builtin",
      "NAME",
      "Print compiled-in Flux configuration values",
      builtin_get,
      0,
      builtin_opts,
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
