/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell mainline */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"

#include "info.h"
#include "run.h"
#include "procs.h"

static char *shell_name = "flux-shell";
static const char *shell_usage = "[OPTIONS] jobid broker-rank";

static struct optparse_option shell_opts[] =  {
    { .name = "jobspec", .key = 'j', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get jobspec from FILE, not from Flux KVS", },
    { .name = "resources", .key = 'r', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get R from FILE, not from Flux KVS", },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Log actions to stderr", },
    OPTPARSE_TABLE_END
};

/* Parse optarg as a jobid rank and assign to 'jobid'.
 * Return 0 on success or -1 on failure (log error).
 */
static int parse_jobid (const char *optarg, flux_jobid_t *jobid)
{
    unsigned long long i;
    char *endptr;

    errno = 0;
    i = strtoull (optarg, &endptr, 10);
    if (errno != 0) {
        log_err ("error parsing jobid");
        return -1;
    }
    if (*endptr != '\0') {
        log_msg ("error parsing jobid: garbage follows number");
        return -1;
    }
    *jobid = i;
    return 0;
}

/* Parse optarg as a broker rank and assign to 'rank'.
 * Return 0 on success or -1 on failure (log error).
 */
static int parse_broker_rank (const char *optarg, uint32_t *rank)
{
    unsigned long i;
    char *endptr;

    errno = 0;
    i = strtoul (optarg, &endptr, 10);
    if (errno != 0) {
        log_err ("error parsing broker-rank");
        return -1;
    }
    if (*endptr != '\0') {
        log_msg ("error parsing broker-rank: garbage follows number");
        return -1;
    }
    *rank = i;
    return 0;
}

/* Read content of file 'optarg' and return it or NULL on failure (log error).
 * Caller must free returned result.
 */
static char *parse_arg_file (const char *optarg)
{
    int fd;
    ssize_t size;
    void *buf = NULL;

    if (!strcmp (optarg, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (optarg, O_RDONLY)) < 0) {
            log_err ("error opening %s", optarg);
            return NULL;
        }
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err ("error reading %s", optarg);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    return buf;
}

int main (int argc, char *argv[])
{
    optparse_t *p;
    int optindex;
    struct shell_info *info;
    int rc;
    flux_reactor_t *r;
    flux_t *h;
    flux_jobid_t jobid;
    uint32_t broker_rank;
    char *jobspec = NULL;
    char *R = NULL;
    struct shell_procs *procs;

    log_init (shell_name);

    p = optparse_create (shell_name);
    if (optparse_add_option_table (p, shell_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table failed");
    if (optparse_set (p, OPTPARSE_USAGE, shell_usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage failed");
    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    /* Parse required positional arguments: jobid broker_rank.
     */
    if (optindex != argc - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (parse_jobid (argv[optindex++], &jobid) < 0)
        exit (1);
    if (parse_broker_rank (argv[optindex++], &broker_rank) < 0)
        exit (1);

    /* Optional:  Parse jobspec and R from files.
     * Otherwise shell_info_create() will fetch them from job-info service.
     */
    if (optparse_hasopt (p, "jobspec")) {
        jobspec = parse_arg_file (optparse_get_str (p, "jobspec", NULL));
        if (!jobspec)
            exit (1);
    }
    if (optparse_hasopt (p, "resources")) {
        R = parse_arg_file (optparse_get_str (p, "resources", NULL));
        if (!R)
            exit (1);
    }

    /* Get broker connection and reactor capable of monitoring subprocesses.
     */
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");
    flux_set_reactor (h, r);

    /* Populate 'struct shell_info' for general use by shell components.
     */
    if (!(info = shell_info_create (h,
                                    jobid,
                                    broker_rank,
                                    jobspec,
                                    R,
                                    optparse_hasopt (p, "verbose"))))
        exit (1);

    /* Start subprocesses.
     */
    if (!(procs = shell_procs_create (h, info)))
        exit (1);

    /* Main reactor loop
     */
    rc = shell_run (h, info);

    shell_procs_destroy (procs);
    shell_info_destroy (info);

    flux_close (h);

    free (jobspec);
    free (R);

    optparse_destroy (p);
    log_fini ();

    exit (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
