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
#include <stdarg.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/log.h"

#include "shell.h"
#include "info.h"
#include "svc.h"
#include "io.h"
#include "pmi.h"
#include "task.h"

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

static void task_completion_cb (struct shell_task *task, void *arg)
{
    struct flux_shell *shell = arg;

    if (shell->verbose)
        log_msg ("task %d complete status=%d", task->rank, task->rc);

    if (flux_shell_remove_completion_ref (shell, "task%d", task->rank) < 0)
        log_err ("failed to remove task%d completion reference", task->rank);
}

static void shell_parse_cmdline (flux_shell_t *shell, int argc, char *argv[])
{
    int optindex;
    optparse_t *p = optparse_create (shell_name);

    if (p == NULL)
        log_msg_exit ("optparse_create");
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
    if (parse_jobid (argv[optindex++], &shell->jobid) < 0)
        exit (1);

    /* In standalone mode, jobspec, resources and broker-rank must be
     *  set on command line:
     */
    if ((shell->standalone = optparse_hasopt (p, "standalone"))) {
        if (  !optparse_hasopt (p, "jobspec")
           || !optparse_hasopt (p, "resources")
           || !optparse_hasopt (p, "broker-rank"))
            log_err_exit ("standalone mode requires --jobspec, "
                          "--resources and --broker-rank");
    }

    shell->verbose = optparse_getopt (p, "verbose", NULL);
    shell->broker_rank = optparse_get_int (p, "broker-rank", -1);
    shell->p = p;
}

static void shell_connect_flux (flux_shell_t *shell)
{
    if (!(shell->h = flux_open (shell->standalone ? "loop://" : NULL, 0)))
        log_err_exit ("flux_open");

    /*  Set reactor for flux handle to our custom created reactor.
     */
    flux_set_reactor (shell->h, shell->r);

    /*  Fetch local rank if not already set
     */
    if (shell->broker_rank < 0) {
        uint32_t rank;
        if (flux_get_rank (shell->h, &rank) < 0)
            log_err ("error fetching broker rank");
        shell->broker_rank = rank;
    }
}

static void shell_finalize (flux_shell_t *shell)
{
    /* Process completed tasks:
     * - reduce exit codes to shell 'rc'
     * - destroy
     */
    shell->rc = 0;
    if (shell->tasks) {
        struct shell_task *task;

        while ((task = zlist_pop (shell->tasks))) {
            if (shell->rc < task->rc)
                shell->rc = task->rc;
            shell_task_destroy (task);
        }
        zlist_destroy (&shell->tasks);
    }
    shell_io_destroy (shell->io);
    shell_pmi_destroy (shell->pmi);
    shell_svc_destroy (shell->svc);
    shell_info_destroy (shell->info);

    flux_reactor_destroy (shell->r);
    flux_close (shell->h);

    optparse_destroy (shell->p);

    zhashx_destroy (&shell->completion_refs);
}

static void item_free (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static void shell_init (flux_shell_t *shell)
{
    memset (shell, 0, sizeof (struct flux_shell));
    if (!(shell->completion_refs = zhashx_new ()))
        log_err_exit ("zhashx_new");
    zhashx_set_destructor (shell->completion_refs, item_free);
}

void flux_shell_killall (flux_shell_t *shell, int signum)
{
    struct shell_task *task;
    task = zlist_first (shell->tasks);
    while (task) {
        if (shell_task_kill (task, signum) < 0)
            log_err ("kill task %d: signal %d", task->rank, signum);
        task = zlist_next (shell->tasks);
    }
}

int flux_shell_add_completion_ref (flux_shell_t *shell,
                                   const char *fmt, ...)
{
    int rc = -1;
    int *intp = NULL;
    char *ref = NULL;
    va_list ap;

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0)
        goto out;
    if (!(intp = zhashx_lookup (shell->completion_refs, ref))) {
        if (!(intp = calloc (1, sizeof (*intp))))
            goto out;
        if (zhashx_insert (shell->completion_refs, ref, intp) < 0) {
            free (intp);
            goto out;
        }
    }
    rc = ++(*intp);
out:
    free (ref);
    va_end (ap);
    return (rc);

}

int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                      const char *fmt, ...)
{
    int rc = -1;
    int *intp;
    va_list ap;
    char *ref = NULL;

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0)
        goto out;
    if (!(intp = zhashx_lookup (shell->completion_refs, ref))) {
        errno = ENOENT;
        goto out;
    }
    if (--(*intp) == 0) {
        zhashx_delete (shell->completion_refs, ref);
        if (zhashx_size (shell->completion_refs) == 0)
            flux_reactor_stop (shell->r);
    }
    rc = 0;
out:
    free (ref);
    va_end (ap);
    return (rc);
}

static int shell_barrier (flux_shell_t *shell, const char *name)
{
    flux_future_t *f;
    char fqname[128];

    if (shell->standalone || shell->info->shell_size == 1)
        return 0; // NO-OP
    if (snprintf (fqname,
                  sizeof (fqname),
                  "shell-%ju-%s",
                  (uintmax_t)shell->info->jobid,
                   name) >= sizeof (fqname)) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_barrier (shell->h, fqname, shell->info->shell_size)))
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int main (int argc, char *argv[])
{
    flux_shell_t shell;
    int i;

    log_init (shell_name);

    shell_init (&shell);

    shell_parse_cmdline (&shell, argc, argv);

    /* Get reactor capable of monitoring subprocesses.
     */
    if (!(shell.r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");

    /* Connect to broker, or if standalone, open loopback connector.
     */
    shell_connect_flux (&shell);

    /* Populate 'struct shell_info' for general use by shell components.
     * Fetches missing info from shell handle if set.
     */
    if (!(shell.info = shell_info_create (&shell)))
        exit (1);

    /* Register service on the leader shell.
     */
    if (!(shell.svc = shell_svc_create (&shell)))
        log_err_exit ("shell_svc_create");

    /* Create PMI engine
     * Uses 'h' for KVS access only if info->shell_size > 1.
     * Tasks send PMI wire proto to pmi.c via shell_pmi_task_ready() callback
     * registered below.
     */
    if (!(shell.pmi = shell_pmi_create (&shell)))
        log_err_exit ("shell_pmi_create");

    /* Create handler for stdio.
     */
    if (!(shell.io = shell_io_create (&shell)))
        log_err_exit ("shell_io_create");

    /* Barrier to ensure initialization has completed across all shells.
     */
    if (shell_barrier (&shell, "init") < 0)
        log_err_exit ("shell_barrier");

    /* Create tasks
     */
    if (!(shell.tasks = zlist_new ()))
        log_msg_exit ("zlist_new failed");
    for (i = 0; i < shell.info->rankinfo.ntasks; i++) {
        struct shell_task *task;

        if (!(task = shell_task_create (shell.info, i)))
            log_err_exit ("shell_task_create index=%d", i);
        if (shell_task_pmi_enable (task, shell_pmi_task_ready, shell.pmi) < 0)
            log_err_exit ("shell_task_pmi_enable");
        if (shell_task_io_enable (task, shell_io_task_ready, shell.io) < 0)
            log_err_exit ("shell_task_io_enable");
        if (shell_task_start (task, shell.r, task_completion_cb, &shell) < 0)
            log_err_exit ("shell_task_start index=%d", i);

        if (zlist_append (shell.tasks, task) < 0)
            log_msg_exit ("zlist_append failed");

        if (flux_shell_add_completion_ref (&shell, "task%d", task->rank) < 0)
            log_msg_exit ("flux_shell_add_completion_ref");

    }

    /* Main reactor loop
     * Exits when all completion references released
     */
    if (flux_reactor_run (shell.r, 0) < 0)
        log_err ("flux_reactor_run");

    shell_finalize (&shell);

    if (shell.verbose)
        log_msg ("exit %d", shell.rc);

    log_fini ();
    exit (shell.rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
