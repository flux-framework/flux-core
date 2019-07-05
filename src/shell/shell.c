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
#include <czmq.h>
#include <flux/core.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"

#include "info.h"
#include "task.h"
#include "pmi.h"
#include "io.h"

static char *shell_name = "flux-shell";
static const char *shell_usage = "[OPTIONS] JOBID";

static struct optparse_option shell_opts[] =  {
    { .name = "jobspec", .key = 'j', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get jobspec from FILE, not job-info service", },
    { .name = "resources", .key = 'R', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get R from FILE, not job-info service", },
    { .name = "broker-rank", .key = 'r', .has_arg = 1, .arginfo = "RANK",
      .usage = "Set broker rank, rather than asking broker", },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Log actions to stderr", },
    { .name = "standalone", .key = 's', .has_arg = 0,
      .usage = "Run local program without Flux instance", },
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

static void task_completion_cb (struct shell_task *task, void *arg)
{
    struct shell_info *info = arg;

    if (info->verbose)
        log_msg ("task %d complete status=%d", task->rank, task->rc);
}

int main (int argc, char *argv[])
{
    optparse_t *p;
    int optindex;
    struct shell_info *info;
    int rc;
    flux_reactor_t *r;
    flux_t *h = NULL;
    flux_jobid_t jobid;
    int broker_rank = -1;
    char *jobspec = NULL;
    char *R = NULL;
    int i;
    zlist_t *tasks;
    struct shell_pmi *pmi;
    struct shell_io *io;

    log_init (shell_name);

    p = optparse_create (shell_name);
    if (optparse_add_option_table (p, shell_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table failed");
    if (optparse_set (p, OPTPARSE_USAGE, shell_usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage failed");
    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    /* Parse required positional argument.
     */
    if (optindex != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (parse_jobid (argv[optindex++], &jobid) < 0)
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
    broker_rank = optparse_get_int (p, "broker-rank", -1);

    /* Get reactor capable of monitoring subprocesses.
     */
    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");

    /* In standalone mode, broker connection is unavailable.
     */
    if (optparse_hasopt (p, "standalone")) {
        if (!jobspec)
            log_err_exit ("--jobspec is required in standalone mode");
        if (!R)
            log_err_exit ("--resources is required in standalone mode");
        if (broker_rank == -1)
            log_err_exit ("--broker-rank is required in standalone mode");
    }
    else {
        if (!(h = flux_open (NULL, 0)))
            log_err_exit ("flux_open");
        flux_set_reactor (h, r);
    }

    /* Populate 'struct shell_info' for general use by shell components.
     * Fetches missing info from 'h' if set.
     */
    if (!(info = shell_info_create (h,
                                    jobid,
                                    broker_rank,
                                    jobspec,
                                    R,
                                    optparse_hasopt (p, "verbose"))))
        exit (1);

    /* Create PMI engine
     * Uses 'h' for KVS access only if info->shell_size > 1.
     * Tasks send PMI wire proto to pmi.c via shell_pmi_task_ready() callback
     * registered below.
     */
    if (!(pmi = shell_pmi_create (h, info)))
        log_err_exit ("shell_pmi_create");

    /* Create handler for stdio.
     */
    if (!(io = shell_io_create (h, info)))
        log_err_exit ("shell_io_create");

    /* Create tasks
     */
    if (!(tasks = zlist_new ()))
        log_msg_exit ("zlist_new failed");
    for (i = 0; i < info->rankinfo.ntasks; i++) {
        struct shell_task *task;

        if (!(task = shell_task_create (info, i)))
            log_err_exit ("shell_task_create index=%d", i);
        if (shell_task_pmi_enable (task, shell_pmi_task_ready, pmi) < 0)
            log_err_exit ("shell_task_pmi_enable");
        //if (shell_task_io_enable (task, shell_io_task_ready, io) < 0)
        //    log_err_exit ("shell_task_io_enable");
        if (shell_task_start (task, r, task_completion_cb, info) < 0)
            log_err_exit ("shell_task_start index=%d", i);

        if (zlist_append (tasks, task) < 0)
            log_msg_exit ("zlist_append failed");

    }

    /* Main reactor loop
     * Exits once final task exits.
     */
    if (flux_reactor_run (r, 0) < 0)
        log_err ("flux_reactor_run");

    /* Destroy completed tasks, reducing exit codes to 'rc'.
     */
    rc = 0;
    if (tasks) {
        struct shell_task *task;

        while ((task = zlist_pop (tasks))) {
            if (rc < task->rc)
                rc = task->rc;
            shell_task_destroy (task);
        }
        zlist_destroy (&tasks);
    }
    shell_io_destroy (io);
    shell_pmi_destroy (pmi);

    shell_info_destroy (info);

    flux_reactor_destroy (r);

    if (!optparse_hasopt (p, "standalone")) {
        flux_close (h);
    }

    free (jobspec);
    free (R);

    optparse_destroy (p);
    log_fini ();

    if (info->verbose)
        log_msg ("exit %d", rc);
    exit (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
