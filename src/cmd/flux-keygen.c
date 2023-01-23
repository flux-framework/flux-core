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
#include <unistd.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <czmq.h>

#include "src/common/libutil/log.h"

static struct optparse_option opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Set certificate name (default: hostname)", },
    { .name = "meta", .has_arg = 1, .arginfo = "KEYVALS",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Add/update comma-separated key=value metadata", },
    OPTPARSE_TABLE_END,
};

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        return (NULL);
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

int main (int argc, char *argv[])
{
    const char *usage_msg = "[OPTIONS] [PATH]";
    optparse_t *p;
    int optindex;
    zcert_t *cert;
    const char *name;
    char now[64];
    char hostname[64];
    char *path = NULL;

    log_init ("flux-keygen");
    if (!(p = optparse_create ("flux-keygen"))
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS
        || optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_err_exit ("error setting up otpion parsing");
    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if (optindex < argc)
        path = argv[optindex++];
    if (optindex < argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!path)
        log_msg ("WARNING: add PATH argument to save generated certificate");

    if (!(cert = zcert_new ()))
        log_msg_exit ("zcert_new: %s", zmq_strerror (errno));
    if (gethostname (hostname, sizeof (hostname)) < 0)
        log_err_exit ("gethostname");
    if (ctime_iso8601_now (now, sizeof (now)) == NULL)
        log_err_exit ("localtime");

    /* N.B. zcert_set_meta() silently ignores attempts to set the same
     * value twice so the default settings come later and are ignored
     * if values were provided on the command line.
     */
    if ((name = optparse_get_str (p, "name", NULL))) {
        zcert_set_meta (cert, "name", "%s", name);
    }
    if (optparse_hasopt (p, "meta")) {
        const char *arg;

        optparse_getopt_iterator_reset (p, "meta");
        while ((arg = optparse_getopt_next (p, "meta"))) {
            char *key;
            char *val;
            if (!(key = strdup (arg)))
                log_msg_exit ("out of memory");
            if ((val = strchr (key, '=')))
                *val++ = '\0';
            zcert_set_meta (cert, key, "%s", val ? val : "");
            free (key);
        }
    }

    // name is used in overlay logging
    zcert_set_meta (cert, "name", "%s", hostname);
    zcert_set_meta (cert, "hostname", "%s", hostname);
    zcert_set_meta (cert, "time", "%s", now);
    zcert_set_meta (cert, "userid", "%d", getuid ());

    if (path && zcert_save_secret (cert, path) < 0)
        log_msg_exit ("zcert_save_secret %s: %s", path, strerror (errno));

    zcert_destroy (&cert);

    optparse_destroy (p);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
