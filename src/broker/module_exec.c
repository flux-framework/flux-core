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
 * Usage: flux module-exec MODULE
 * This mode is used when 'flux module load --exec MODULE' is run.
 * FLUX_MODULE_URI will be set to the module's handle.
 *
 * test mode
 * Usage: flux module-exec [--name=NAME] MODULE [args...]
 * This mode can be used to manually set up broker modules for debugging.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
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

static const double status_timeout = 10.;

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

static int attr_cache_from_json (flux_t *h, json_t *cache)
{
    const char *name;
    json_t *o;

    json_object_foreach (cache, name, o) {
        const char *val = json_string_value (o);
        if (flux_attr_set_cacheonly (h, name, val) < 0)
            return -1;
    }
    return 0;
}

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

static int args_from_json (struct modexec *me, json_t *args)
{
    if (!json_is_null (args)) {
        size_t index;
        json_t *entry;

        json_array_foreach (args, index, entry) {
            const char *s = json_string_value (entry);
            if (s && (argz_add (&me->argz, &me->argz_len, s) != 0)) {
                errno = ENOMEM;
                return -1;
            }
        }
    }
    me->argc = argz_count (me->argz, me->argz_len);
    if (!(me->argv = calloc (me->argc + 1, sizeof (me->argv[0]))))
        return -1;
    argz_extract (me->argz, me->argz_len, me->argv);
    return 0;
}

/* Decode welcome message and
 * - set me->name, me->uuid
 * - set me->argc, me->argv
 * - populate the broker attr cache in me->h
 */
static void broker_mode_init (struct modexec *me)
{
    flux_msg_t *msg;
    json_t *args;
    json_t *attrs;
    json_t *conf;
    const char *name;
    const char *uuid;

    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .matchtag = FLUX_MATCHTAG_NONE,
        .topic_glob = "welcome",
    };
    if (!(msg = flux_recv (me->h, match, 0)))
        log_err_exit ("welcome receive failure");
    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:o s:o s:s s:s}",
                             "args", &args,
                             "attrs", &attrs,
                             "conf", &conf,
                             "name", &name,
                             "uuid", &uuid) < 0)
        log_err_exit ("welcome decode failure");
    if (!(me->name = strdup (name))
        || !(me->uuid = strdup (uuid))
        || attr_cache_from_json (me->h, attrs) < 0
        || config_cache_from_json (me->h, conf) < 0
        || args_from_json (me, args) < 0)
        log_err_exit ("welcome failed");
    flux_msg_decref (msg);
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
    me->dso = module_dso_open (me->path, me->name, &me->mod_main, &error);
    if (!me->dso)
        log_err_exit ("%s", error.text);
}

int main (int argc, char *argv[])
{
    struct modexec me = { 0 };
    int optindex;
    const char *module;
    const char *uri;
    bool test_mode = true;
    int mod_main_errno = 0;

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

    flux_error_t error;
    if (!(me.h = flux_open_ex (uri, 0, &error)))
        log_msg_exit ("flux_open: %s", error.text);
    if (test_mode) {
        log_msg ("loading module in test mode");
        test_mode_init (&me, module, argc - optindex, argv + optindex);
    }
    else {
        broker_mode_init (&me);
        flux_log_set_appname (me.h, me.name);
    }

    /* Set flux::uuid and flux::name per RFC 5
     */
    if (flux_aux_set (me.h, "flux::uuid", me.uuid, NULL) < 0
        || flux_aux_set (me.h, "flux::name", me.name, NULL) < 0)
        log_err_exit ("error setting flux:: attributes");

    /* Register standard module services
     */
    if (modservice_register (me.h) < 0)
        log_err_exit ("error registering internal services");

    /* Load the DSO and set me->path
     */
    modexec_load (&me, module);

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
        flux_future_t *f;
        flux_msg_t *msg;

        // set FINALIZING state (mutes module)
        if (!(f = flux_rpc_pack (me.h,
                                 "module.status",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:i}",
                                 "status", FLUX_MODSTATE_FINALIZING))
            || flux_future_wait_for (f, status_timeout) < 0
            || flux_rpc_get (f, NULL) < 0) {
            log_msg_exit ("module status (FINALIZING): %s",
                          future_strerror (f, errno));
        }
        flux_future_destroy (f);

        // respond to unhandled requests
        while ((msg = flux_recv (me.h, FLUX_MATCH_REQUEST, FLUX_O_NONBLOCK))) {
            const char *topic = "unknown";
            (void)flux_msg_get_topic (msg, &topic);
            flux_log (me.h,
                      LOG_DEBUG,
                      "responding to post-shutdown %s",
                      topic);
            if (flux_respond_error (me.h, msg, ENOSYS, NULL) < 0)
                flux_log_error (me.h, "responding to post-shutdown %s", topic);
            flux_msg_destroy (msg);
        }

        // set EXITED status (fire and forget due to mute above)
        if (!(f = flux_rpc_pack (me.h,
                                 "module.status",
                                 FLUX_NODEID_ANY,
                                 FLUX_RPC_NORESPONSE,
                                 "{s:i s:i}",
                                 "status", FLUX_MODSTATE_EXITED,
                                 "errnum", mod_main_errno)))
            log_err_exit ("module.status (EXITED)");
        flux_future_destroy (f);
    }

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
