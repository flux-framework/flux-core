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
 *
 * There are two modes:
 *
 * broker mode
 * Usage: flux module-exec PATH
 * This mode is used when 'flux module load --exec MODULE' is run.
 * FLUX_MODULE_URI will be set to the module's handle.
 *
 * test mode
 * Usage: flux module-exec [--name=NAME] PATH [args...]
 * This mode can be used to manually set up broker modules for debugging.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
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
#include "src/common/libutil/errno_safe.h"

#include "module.h"
#include "module_dso.h"

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
    char *modargs;
};

static const char *cmdname = "flux-module-exec";
static const char *cmdusage = "[OPTIONS] PATH ARGS...";

static struct optparse_option cmdopts[] = {
    {
        .name = "name",
        .has_arg = 1,
        .arginfo = "NAME",
        .usage = "Override module name",
    },
    OPTPARSE_TABLE_END
};

/* Module arguments are provided by flux_module_initialize() as a
 * space-delimited string, or NULL if there are no arguments.
 * Translate to argz vector, stored in 'me'.
 */
static int parse_modargs (struct modexec *me, const char *s)
{
    if (s) {
        error_t e;

        if ((e = argz_create_sep (s, ' ', &me->argz, &me->argz_len)) != 0) {
            errno = e;
            return -1;
        }
        me->argc = argz_count (me->argz, me->argz_len);
        if (!(me->argv = calloc (1, sizeof (me->argv[0]) * (me->argc + 1))))
            return -1;
        argz_extract (me->argz, me->argz_len, me->argv);
    }
    return 0;
}

/* Slightly silly - this is just to massage argc, argv into expected form
 * so that tear-down can be the same for both modes.
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
    flux_conf_t *cf = NULL;
    int rc = -1;

    if (!(f = flux_rpc (me->h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "o", &conf) < 0
        || !(cf = flux_conf_pack ("O", conf))
        || flux_set_conf_new (me->h, cf) < 0) {
        flux_conf_decref (cf);
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
static int fake_the_uuid (flux_t *h)
{
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];
    char *cpy;

    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);

    if (!(cpy = strdup (uuid_str))
        || flux_aux_set (h, "flux::uuid", cpy, (flux_free_f)free) < 0) {
        ERRNO_SAFE_WRAP (free, cpy);
        return -1;
    }
    return 0;
}

static void test_mode_init (struct modexec *me,
                            const char *path,
                            int argc,
                            char **argv)
{
    // use --name=NAME or heuristic based on MODULE argument
    const char *nameopt = optparse_get_str (me->opts, "name", NULL);
    char *name = NULL;

    if (nameopt) {
        if (!(name = strdup (nameopt)))
            log_err_exit ("error duplicating module name");
    }
    else  {
        if (!(name = module_name_frompath (path)))
            log_err_exit ("error determining module name");
    }
    if (flux_aux_set (me->h, "flux::name", name, (flux_free_f)free) < 0)
        log_err_exit ("error setting flux::name");

    if (args_from_argv (me, argc, argv) < 0
        || config_cache_from_broker (me) < 0
        || attr_cache_from_broker (me) < 0
        || fake_the_uuid (me->h) < 0)
        log_err_exit ("test mode initialization failed");

    // register name as a service
    flux_future_t *f;
    if (!(f = flux_service_register (me->h, name))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg_exit ("error registering %s service: %s",
                      name,
                      future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

static void modexec_load (struct modexec *me, const char *path)
{
    const char *name = flux_aux_get (me->h, "flux::name");
    flux_error_t error;

    if (!(me->path = strdup (path)))
        log_err_exit ("error duplicating path");
    if (!(me->dso = module_dso_open (me->path, name, &me->mod_main, &error)))
        log_err_exit ("%s", error.text);
}

int main (int argc, char *argv[])
{
    struct modexec me = { 0 };
    int optindex;
    const char *path;
    const char *uri;
    bool test_mode = true;
    int mod_main_errno = 0;
    flux_error_t error;

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
    path = argv[optindex++];

    /* If the broker is starting this program as a sub-process, it will
     * set FLUX_MODULE_URI in the environment  Otherwise, assume "test mode"
     * where the user wants to run the DSO independently (for example to
     * debug it).
     */
    if ((uri = getenv ("FLUX_MODULE_URI"))) {
        test_mode = false;
        if (optindex < argc)
            log_msg_exit ("FLUX_MODULE_URI and free arguments"
                          " are incompatible");
        if (optparse_hasopt (me.opts, "name"))
            log_msg_exit ("FLUX_MODULE_URI and --name are incompatible");
    }

    if (!(me.h = flux_open_ex (uri, 0, &error)))
        log_msg_exit ("flux_open: %s", error.text);
    if (test_mode) {
        log_msg ("loading module in test mode");
        test_mode_init (&me, path, argc - optindex, argv + optindex);
    }
    else {
        char *modargs;
        if (flux_module_initialize (me.h, &modargs, &error) < 0)
            log_msg_exit ("initialize error: %s", error.text);
        if (parse_modargs (&me, modargs) < 0)
            log_err_exit ("error parsing module arguments");
        free (modargs);

#ifdef PR_SET_NAME
        const char *name;
        name = flux_aux_get (me.h, "flux::name");
        (void)prctl (PR_SET_NAME, name, 0, 0, 0);
#endif
    }

    /* Register standard module services
     */
    if (flux_module_register_handlers (me.h, &error) < 0)
        log_err_exit ("error registering internal services: %s", error.text);

    /* Load the DSO and set me->path
     */
    modexec_load (&me, path);

    /* Run the DSO mod_main()
     */
    if (me.mod_main (me.h, me.argc, me.argv) < 0) {
        if (errno == 0)
            errno = ECONNRESET;
        if (test_mode)
            log_err_exit ("module failed");
        else
            flux_log (me.h, LOG_CRIT, "module exiting abnormally");
        mod_main_errno = errno;
    }

    if (!test_mode) {
        if (flux_module_finalize (me.h, mod_main_errno, &error) < 0)
            log_msg_exit ("error finalizing module: %s", error.text);
    }

    free (me.argv);
    free (me.argz);
    free (me.modargs);
    flux_close (me.h);
    module_dso_close (me.dso);
    free (me.path);
    optparse_destroy (me.opts);
    log_fini ();

    exit (0);
}

// vi:ts=4 sw=4 expandtab
