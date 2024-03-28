/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/broker/state_machine.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libutil/uri.h"

#include "builtin.h"

static void get_kvs_version (flux_t *h, int *version)
{
    (*version) = 0;
    if (flux_kvs_get_version (h, NULL, version) < 0
        && errno != ENOSYS)
        log_err_exit ("Error fetching KVS version");
}

static void get_gc_threshold (flux_t *h, int *gc_threshold)
{
    flux_future_t *f;
    json_t *o;
    (*gc_threshold) = 0;
    if (!(f = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f, "o", &o) < 0)
        log_msg_exit ("Error fetching flux config: %s",
                      future_strerror (f, errno));
    (void)json_unpack (o, "{s:{s:i}}", "kvs", "gc-threshold", gc_threshold);
}

static int askyn (char *prompt, bool default_value, bool *result)
{
    while (1) {
        char buf[16];
        printf ("%s [%s]? ", prompt, default_value ? "Y/n" : "y/N");
        fflush (stdout);
        if (fgets (buf, sizeof (buf), stdin) == NULL)
            return -1;
        if (buf[0] == '\n')
            break;
        if (buf[0] == 'y' || buf[0] == 'Y') {
            (*result) = true;
            return 0;
        }
        if (buf[0] == 'n' || buf[0] == 'N') {
            (*result) = false;
            return 0;
        }
        printf ("Please answer y or n\n");
    };
    (*result) = default_value;
    return 0;
}

static bool gc_threshold_check (flux_t *h, optparse_t *p)
{
    int gc_threshold, version;
    bool rc = false;

    get_kvs_version (h, &version);
    get_gc_threshold (h, &gc_threshold);

    if (gc_threshold > 0 && version > gc_threshold) {
        if (optparse_hasopt (p, "yes")
            || optparse_hasopt (p, "no")
            || optparse_hasopt (p, "skip-gc")) {
            if (optparse_hasopt (p, "yes"))
                rc = true;
            else
                rc = false;
            return rc;
        }

        if (!isatty (STDIN_FILENO))
            log_msg_exit ("gc threshold exceeded, specify -y or -n\n");

        if (askyn ("gc threshold exceeded, "
                   "do you want to perform garbage collection",
                   true,
                   &rc) < 0)
            log_msg_exit ("error retrieving user input");
    }
    return rc;
}

static void process_updates (flux_future_t *f)
{
    const char *s;

    while (flux_rpc_get_unpack (f, "{s:s}", "log", &s) == 0) {
        fprintf (stderr, "%s", s);
        flux_future_reset (f);
    }
    if (errno != ENODATA)
        log_msg_exit ("%s", future_strerror (f, errno));
}

static int subcmd (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;
    int flags = FLUX_RPC_STREAMING;
    int optindex = optparse_option_index (p);
    bool quiet = optparse_hasopt (p, "quiet");
    int verbose = optparse_get_int (p, "verbose", 0);
    int loglevel = quiet ? LOG_WARNING
                 : verbose == 0 ? LOG_INFO : LOG_DEBUG;
    const char *target = NULL;

    log_init ("flux-shutdown");

    if (optindex < ac)
        target = av[optindex++];
    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (target) {
        char *uri = uri_resolve (target, NULL);
        if (!uri)
            log_msg_exit ("failed to resolve target %s to a Flux URI", target);
        if (!(h = flux_open (uri, 0)))
            log_err_exit ("error connecting to Flux");
        free (uri);
    }
    else {
        if (!(h = flux_open (NULL, 0)))
            log_err_exit ("error connecting to Flux");
    }

    if (optparse_hasopt (p, "background"))
        flags &= ~FLUX_RPC_STREAMING;

    if (optparse_hasopt (p, "skip-gc")) {
        if (flux_attr_set (h, "content.dump", "") < 0)
            log_err_exit ("error clearing content.dump attribute");
    }

    if (optparse_hasopt (p, "gc")
        || optparse_hasopt (p, "dump")
        || gc_threshold_check (h, p)) {
        const char *val = optparse_get_str (p, "dump", "auto");

        if (flux_attr_set (h, "content.dump", val) < 0)
            log_err_exit ("error setting content.dump attribute");

        log_msg ("shutdown will dump KVS (this may take some time)");
    }

    /* N.B. set nodeid=FLUX_NODEID_ANY so we get immediate error from
     * broker if run on rank > 0.
     */
    if (!(f = flux_rpc_pack (h,
                             "shutdown.start",
                             FLUX_NODEID_ANY,
                             flags,
                             "{s:i}",
                             "loglevel", loglevel)))
        log_err_exit ("could not send shutdown.start request");

    if ((flags & FLUX_RPC_STREAMING))
        process_updates (f);
    else if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%s", future_strerror (f, errno));

    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

static struct optparse_option opts[] = {
    { .name = "skip-gc", .has_arg = 0,
      .usage = "Skip KVS garbage collection this time, if already enabled",
    },
    { .name = "gc", .has_arg = 0,
      .usage = "Garbage collect KVS (short for --dump=auto)",
    },
    { .name = "dump", .has_arg = 1, .arginfo = "PATH",
      .usage = "Dump KVS content to specified archive file using flux-dump(1)."
    },
    { .name = "background", .has_arg = 0,
      .usage = "Exit the command immediately after initiating shutdown",
    },
    { .name = "quiet", .has_arg = 0,
      .usage = "Show only log messages <= LOG_WARNING level",
    },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Increase log verbosity:"
               " 0=show log messages <= LOG_INFO level (default),"
               " 1=show all log messages",
    },
    { .name = "yes", .key = 'y', .has_arg = 0,
      .usage = "Answer yes to any yes/no questions",
    },
    { .name = "no", .key = 'n', .has_arg = 0,
      .usage = "Answer no to any yes/no questions",
    },
    OPTPARSE_TABLE_END
};

int subcommand_shutdown_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
        "shutdown",
        subcmd,
        "[OPTIONS] [TARGET]",
        "Shut down the Flux instance",
        0,
        opts);
    if (e != OPTPARSE_SUCCESS)
        return -1;
    return 0;
}

// vi: ts=4 sw=4 expandtab
