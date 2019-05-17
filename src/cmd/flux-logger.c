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
#    include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/read_all.h"

#define OPTIONS "hs:n:"
static const struct option longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"severity", required_argument, 0, 's'},
    {"appname", required_argument, 0, 'n'},
    {0, 0, 0, 0},
};

void usage (void)
{
    fprintf (stderr,
             "Usage: flux-logger [--severity LEVEL] [--appname NAME] message "
             "...\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    char *message = NULL;
    size_t len = 0;
    int severity = LOG_NOTICE;
    char *appname = "logger";
    int e;

    log_init ("flux-logger");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'h': /* --help */
            usage ();
            break;
        case 's': /* --severity LEVEL */
            if ((severity = stdlog_string_to_severity (optarg)) < 0)
                log_msg_exit (
                    "invalid severity: Use "
                    "emerg|alert|crit|err|warning|notice|info|debug");
            break;
        case 'n': /* --appname NAME */
            appname = optarg;
            break;
        default:
            usage ();
            break;
        }
    }
    if (optind == argc) {
        len = read_all (STDIN_FILENO, (void **)&message);
    } else {
        if ((e = argz_create (argv + optind, &message, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (message, len, ' ');
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    flux_log_set_appname (h, appname);
    if (flux_log (h, severity, "%s", message) < 0)
        log_err_exit ("flux_log");

    flux_close (h);

    free (message);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
