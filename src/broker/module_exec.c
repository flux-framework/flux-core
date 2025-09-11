/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* module_exec.c - run broker module DSO in another process
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"

#include "module.h"
#include "module_dso.h"
#include "modservice.h"

struct modexec {
    optparse_t *opts;
    char *path;
    void *dso;
    mod_main_f mod_main;
    flux_t *h;
    int argc;
    char **argv;
    size_t argz_len;
    char *argz;
    char *name;
    char *uuid;
};

static const char *cmdname = "flux-module-exec";
static const char *cmdusage = "[OPTIONS] MODULE ARGS...";

static struct optparse_option cmdopts[] = {
    {
        .name = "name",
        .has_arg = 1,
        .arginfo = "NAME",
        .usage = "Override module name",
    },
    OPTPARSE_TABLE_END
};

static int config_cache_from_json (flux_t *h, json_t *conf)
{
    flux_conf_t *cf;

    if (!(cf = flux_conf_pack ("O", conf))
        || flux_set_conf (h, cf) < 0) {
        flux_conf_decref (cf);
        return -1;
    }
    return 0;
}

/* Put args into a form that is compatible with future parsing of
 * a welcome message on a dedicated broker socket.
 */
static int args_from_argv (struct modexec *me, int argc, char **argv)
{
    if (argz_create (argv, &me->argz, &me->argz_len) != 0)
        return -1;
    me->argc = argz_count (me->argz, me->argz_len);
    if (!(me->argv = calloc (1, sizeof (me->argv[0]) * (me->argc + 1))))
        return -1;
    argz_extract (me->argz, me->argz_len, me->argv);
    return 0;
}

/* Fetch the broker config object and cache it in me->h.
 */
static int config_cache_from_broker (struct modexec *me)
{
    flux_future_t *f;
    json_t *conf;
    int rc = -1;

    if (!(f = flux_rpc (me->h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "o", &conf) < 0
        || config_cache_from_json (me->h, conf) < 0) {
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Pre-populate the broker attribute cache.  This isn't strictly necessary
 * and is a bit inefficient, but it replicates the way the broker primes
 * the attribute cache of its modules.  If this isn't done, a module might
 * block on a synchronous RPC in test but not IRL, confusing test results.
 */
static int attr_cache_from_broker (struct modexec *me)
{
    flux_future_t *f;
    json_t *names;
    size_t index;
    json_t *value;

    if (!(f = flux_rpc (me->h, "attr.list", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "{s:o}", "names", &names) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    json_array_foreach (names, index, value) {
        const char *key = json_string_value (value);
        (void)flux_attr_get (me->h, key); // side effect - cache if immutable
    }
    flux_future_destroy (f);
    return 0;
}

/* For the moment set the uuid to a made up value in test mode.
 * N.B. I'm not sure what the fallout of this may be.  Possibly at this
 * point, only that flux-ping(1) may display the wrong endpoint uuid.
 */
static int fake_the_uuid (struct modexec *me)
{
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);

    if (!(me->uuid = strdup (uuid_str)))
        return -1;
    return 0;
}

static void test_mode_init (struct modexec *me,
                            const char *module,
                            int argc,
                            char **argv)
{
    // use --name=NAME or heuristic based on MODULE argument
    const char *name = optparse_get_str (me->opts, "name", NULL);
    if (name)
        me->name = strdup (name);
    else
        me->name = module_dso_name (module);
    if (!me->name)
        log_err_exit ("error duplicating module name");

    if (args_from_argv (me, argc, argv) < 0
        || config_cache_from_broker (me) < 0
        || attr_cache_from_broker (me) < 0
        || fake_the_uuid (me) < 0)
        log_err_exit ("test mode initialization failed");

    // register me->name as a service
    flux_future_t *f;
    if (!(f = flux_service_register (me->h, me->name))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg_exit ("error registering %s service: %s",
                      me->name,
                      future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

static void modexec_load (struct modexec *me, const char *module)
{
    flux_error_t error;

    if (strchr (module, '/')) {
        if (!(me->path = strdup (module)))
            log_err_exit ("error duplicating path");
    }
    else {
        const char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            log_msg_exit ("FLUX_MODULE_PATH is not set in the environment");
        if (!(me->path = module_dso_search (module, searchpath, &error)))
            log_msg_exit ("%s: %s", module, error.text);
    }
    me->dso = module_dso_open (me->path, NULL, &me->mod_main, &error);
    if (!me->dso)
        log_err_exit ("%s", error.text);
}

int main (int argc, char *argv[])
{
    struct modexec me;
    int optindex;
    const char *module;

    log_init ((char *)cmdname);

    if (!(me.opts = optparse_create (cmdname))
        || optparse_set (me.opts, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS
        || optparse_add_option_table (me.opts, cmdopts) != OPTPARSE_SUCCESS
        || (optindex = optparse_parse_args (me.opts, argc, argv)) < 0)
        exit (1);

    if (optindex == argc) {
        optparse_print_usage (me.opts);
        exit (1);
    }
    module = argv[optindex++];

    /* Load the dso and set me->path
     */
    modexec_load (&me, module);

    flux_error_t error;
    if (!(me.h = flux_open_ex (NULL, 0, &error)))
        log_msg_exit ("flux_open: %s", error.text);
    test_mode_init (&me, module, argc - optindex, argv + optindex);

    /* Set flux::uuid and flux::name per RFC 5
     */
    if (flux_aux_set (me.h, "flux::uuid", me.uuid, NULL) < 0
        || flux_aux_set (me.h, "flux::name", me.name, NULL) < 0)
        log_err_exit ("error setting flux:: attributes");

    /* Register standard module services
     */
    if (modservice_register (me.h) < 0)
        log_err_exit ("error registering internal services");

    /* Run the DSO mod_main()
     */
    if (me.mod_main (me.h, me.argc, me.argv) < 0)
        log_err_exit ("module failed");

    free (me.uuid);
    free (me.name);
    free (me.argv);
    free (me.argz);
    flux_close (me.h);
    module_dso_close (me.dso);
    free (me.path);
    optparse_destroy (me.opts);
    log_fini ();

    exit (0);
}

// vi:ts=4 sw=4 expandtab
