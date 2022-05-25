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
    char buf[64];
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

    if (gethostname (buf, sizeof (buf)) < 0)
        log_err_exit ("gethostname");
    zcert_set_meta (cert, "hostname", "%s", buf);
    // name is used in overlay logging
    zcert_set_meta (cert, "name", "%s", optparse_get_str (p, "name", buf));
    zcert_set_meta (cert, "time", "%s", ctime_iso8601_now (buf, sizeof (buf)));
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
