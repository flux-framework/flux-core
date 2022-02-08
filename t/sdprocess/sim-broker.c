/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Emulate job-exec / job-manager in the broker, by "crashing"
 * (exiting) and re-discovering processes running under systemd
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libsdprocess/sdprocess.h"
#include "src/common/libutil/log.h"

int cmd_run (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);
int cmd_run_wait_exit (optparse_t *p, int argc, char **argv);

static struct optparse_option run_opts[] =  {
    { .name = "unitname", .key = 'u', .has_arg = 1,
      .usage = "Specify process unitname",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option run_wait_exit_opts[] =  {
    { .name = "unitname", .key = 'u', .has_arg = 1,
      .usage = "Specify process unitname",
    },
    { .name = "no-cleanup", .key = 'c', .has_arg = 0,
      .usage = "Do not cleanup systemd data no job",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option wait_opts[] =  {
    { .name = "unitname", .key = 'u', .has_arg = 1,
      .usage = "Specify process unitname",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "run",
      "[OPTIONS]",
      "Run command",
      cmd_run,
      0,
      run_opts,
    },
    { "wait",
      "[OPTIONS]",
      "Wait for running process",
      cmd_wait,
      0,
      wait_opts,
    },
    { "run-wait-exit",
      "[OPTIONS]",
      "Run command wait to exit",
      cmd_run_wait_exit,
      0,
      run_wait_exit_opts,
    },
    OPTPARSE_SUBCMD_END
};

static void systemd_cleanup (sdprocess_t *sdp)
{
    int ret = sdprocess_systemd_cleanup (sdp);
    while (ret < 0 && errno == EBUSY) {
        usleep (100000);
        ret = sdprocess_systemd_cleanup (sdp);
    }
}

static char **args2strv (int argc, char **argv)
{
    char **strv = NULL;
    int i;

    /* +1 for NULL pointer at end */
    if (!(strv = calloc (1, sizeof (char *) * (argc + 1))))
        log_err_exit ("calloc");

    for (i = 0; i < argc; i++)
        strv[i] = argv[i];

    return strv;
}

int cmd_run (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    const char *unitname;
    char **cmdv = NULL;
    sdprocess_t *sdp = NULL;
    flux_t *h = NULL;
    bool active;
    int rv = -1;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(unitname = optparse_get_str (p, "unitname", NULL))) {
        optparse_print_usage (p);
        log_err_exit ("must specify unitname");
    }

    if (!(cmdv = args2strv (argc - optindex, &argv[optindex])))
        goto cleanup;

    if (!(h = flux_open (NULL, 0))) {
        log_err ("flux_open");
        goto cleanup;
    }

    if (!(sdp = sdprocess_exec (h,
                                unitname,
                                cmdv,
                                NULL,
                                -1,
                                STDOUT_FILENO,
                                STDERR_FILENO))) {
        log_err ("sdprocess_exec");
        goto cleanup;
    }

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }

    printf ("Unit %s entered active state\n", unitname);
    fflush (stdout);

    rv = 0;
cleanup:
    free (cmdv);
    if (rv < 0)
        systemd_cleanup (sdp);
    sdprocess_destroy (sdp);
    flux_close (h);
    return (rv);
}

static void state_cb (sdprocess_t *sdp,
                      sdprocess_state_t state,
                      void *arg)
{
    const char *unitname = arg;
    if (state == SDPROCESS_ACTIVE)
        printf ("Unit %s entered active state\n", unitname);
    if (state == SDPROCESS_EXITED) {
        int exit_status = sdprocess_exit_status (sdp);
        printf ("Unit %s exited - exit status=%d\n", unitname, exit_status);
    }
    fflush (stdout);
}

int cmd_wait (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    const char *unitname;
    sdprocess_t *sdp = NULL;
    flux_t *h = NULL;
    int rv = -1;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(unitname = optparse_get_str (p, "unitname", NULL))) {
        optparse_print_usage (p);
        log_err_exit ("must specify unitname");
    }

    if (!(h = flux_open (NULL, 0))) {
        log_err ("flux_open");
        goto cleanup;
    }

    if (!(sdp = sdprocess_find_unit (h, unitname))) {
        log_err ("sdprocess_find_unit");
        goto cleanup;
    }

    if (sdprocess_state (sdp, state_cb, (void *)unitname) < 0) {
        log_err ("sdprocess_state");
        goto cleanup;
    }

    printf ("Unit %s attached and monitoring\n", unitname);
    fflush (stdout);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        log_err ("flux_reactor_run");
        goto cleanup;
    }

    rv = 0;
cleanup:
    systemd_cleanup (sdp);
    sdprocess_destroy (sdp);
    flux_close (h);
    return (rv);
}

int cmd_run_wait_exit (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    bool no_cleanup = optparse_hasopt (p, "no-cleanup");
    const char *unitname;
    char **cmdv = NULL;
    sdprocess_t *sdp = NULL;
    flux_t *h = NULL;
    int rv = -1;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(unitname = optparse_get_str (p, "unitname", NULL))) {
        optparse_print_usage (p);
        log_err_exit ("must specify unitname");
    }

    if (!(cmdv = args2strv (argc - optindex, &argv[optindex])))
        goto cleanup;

    if (!(h = flux_open (NULL, 0))) {
        log_err ("flux_open");
        goto cleanup;
    }

    if (!(sdp = sdprocess_exec (h,
                                unitname,
                                cmdv,
                                NULL,
                                -1,
                                STDOUT_FILENO,
                                STDERR_FILENO))) {
        log_err ("sdprocess_exec");
        goto cleanup;
    }

    if (sdprocess_state (sdp, state_cb, (void *)unitname) < 0) {
        log_err ("sdprocess_state");
        goto cleanup;
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        log_err ("flux_reactor_run");
        goto cleanup;
    }

    rv = 0;
cleanup:
    free (cmdv);
    if (!no_cleanup)
        systemd_cleanup (sdp);
    sdprocess_destroy (sdp);
    flux_close (h);
    return (rv);
}

int main (int argc, char *argv[])
{
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("sdprocess-test");

    p = optparse_create ("sdprocess-test");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optindex])) {
        optparse_print_usage (p);
        exit (1);
    }

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
