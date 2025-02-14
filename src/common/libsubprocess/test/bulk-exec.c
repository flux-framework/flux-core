/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Test driver for job-exec/bulk-exec.[ch] bulk subprocess
 *   execution API.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#ifndef HAVE_GET_CURRENT_DIR_NAME
#include "src/common/libmissing/get_current_dir_name.h"
#endif

#include <flux/core.h>
#include <flux/idset.h>
#include <flux/optparse.h>

#include "bulk-exec.h"
#include "src/common/libutil/log.h"
#include "ccan/str/str.h"

extern char **environ;
static int cancel_after = 0;

void started (struct bulk_exec *exec, void *arg)
{
    log_msg ("started");
}

void complete (struct bulk_exec *exec, void *arg)
{
    flux_t *h = arg;
    log_msg ("complete");
    flux_reactor_stop (flux_get_reactor (h));
}

void exited (struct bulk_exec *exec, void *arg, const struct idset *ids)
{
    char *s = idset_encode (ids, IDSET_FLAG_RANGE);
    log_msg ("ranks %s: exited", s);
    free (s);
}

void on_error (struct bulk_exec *exec, flux_subprocess_t *p, void *arg)
{
    if (p) {
        flux_subprocess_state_t state = flux_subprocess_state (p);
        log_msg ("%d: pid %ju: %s",
                 flux_subprocess_rank (p),
                 (uintmax_t)flux_subprocess_pid (p),
                 flux_subprocess_state_string (state));
    }
    flux_future_t *f = bulk_exec_kill (exec, NULL, 9);
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("bulk_exec_kill");
}

void on_output (struct bulk_exec *exec,
                flux_subprocess_t *p,
                const char *stream,
                const char *data,
                int data_len,
                void *arg)
{
    int rank = flux_subprocess_rank (p);
    FILE *fp = streq (stream, "stdout") ? stdout : stderr;
    fprintf (fp, "%d: %s", rank, data);
}

static void kill_cb (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_err ("bulk_exec_kill");
    flux_future_destroy (f);
}

static void signal_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    struct bulk_exec *exec = arg;
    int signum = flux_signal_watcher_get_signum (w);

    log_msg ("sending signal %d to all tasks\n", signum);
    flux_future_t *f = bulk_exec_kill (exec, NULL, signum);
    if (!f || (flux_future_then (f, -1., kill_cb, exec) < 0))
        log_err ("SIGINT: failed to forward signal %d", signum);
    flux_watcher_stop (w);
}

static void check_cancel_cb (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    struct bulk_exec *exec = arg;
    if (cancel_after && bulk_exec_started_count (exec) >= cancel_after) {
        log_msg ("cancelling remaining commands");
        bulk_exec_cancel (exec);
        flux_watcher_stop (w);
    }
}

static unsigned int idset_pop (struct idset *idset)
{
    unsigned int id = idset_first (idset);
    if (id == IDSET_INVALID_ID)
        return id;
    (void) idset_clear (idset, id);
    return id;
}

static struct idset * idset_pop_n (struct idset *idset, int n)
{
    unsigned int id;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);
    if (ids == NULL)
        return NULL;
    while ((n-- > 0) && ((id = idset_pop (idset)) != IDSET_INVALID_ID)) {
        if (idset_set (ids, id) < 0)
            goto err;
    }
    return ids;
err:
    idset_destroy (ids);
    return NULL;
}

void push_commands (struct bulk_exec *exec,
                    struct idset *idset,
                    int ncmds,
                    int ac, char **av)
{
    flux_cmd_t *cmd = flux_cmd_create (ac, av, environ);
    char *cwd = get_current_dir_name ();
    struct idset *ids = NULL;
    int count;
    int per_cmd;

    if (cmd == NULL)
        log_err_exit ("flux_cmd_create");

    if ((count = idset_count (idset)) < ncmds)
        log_err_exit ("Can't split %d ranks into %d cmds", count, ncmds);
    flux_cmd_setcwd (cmd, cwd);
    free (cwd);

    per_cmd = count/ncmds;
    while (idset_count (idset)) {
        struct idset *ids = idset_pop_n (idset, per_cmd);
        if (bulk_exec_push_cmd (exec, ids, cmd, 0) < 0)
            log_err_exit ("bulk_exec_push_cmd");
        idset_destroy (ids);
    }
    idset_destroy (ids);
    flux_cmd_destroy (cmd);
}

int main (int ac, char **av)
{
    struct bulk_exec *exec = NULL;
    optparse_t *p = NULL;
    const char *ranks = NULL;
    struct idset *idset = NULL;
    flux_t *h = NULL;
    uint32_t size;
    int ncmds = 1;
    int optindex = -1;

    struct optparse_option opts[] = {
        { .name = "rank",
          .key = 'r',
          .has_arg = 1,
          .arginfo = "IDS",
          .usage = "Target ranks in IDS"
        },
        { .name = "mpl",
          .key = 'm',
          .has_arg = 1,
          .arginfo = "COUNT",
          .usage = "Max procs to start per loop iteration"
        },
        { .name = "ncmds",
          .key = 'n',
          .has_arg = 1,
          .arginfo = "N",
          .usage = "Internally, split into N 'cmds'"
        },
        { .name = "cancel-after",
          .key  = 'c',
          .has_arg = 1,
          .arginfo = "NCMDS",
          .usage = "Cancel after NCMDS cmds have been launched"
        },
        OPTPARSE_TABLE_END
    };

    struct bulk_exec_ops ops = {
        .on_start    = started,
        .on_exit     = exited,
        .on_complete = complete,
        .on_error    = on_error,
        .on_output   = on_output,
    };

    if (!(p = optparse_create ("execer")))
        log_err_exit ("optparse_create");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (p, ac, av)) < 0)
        exit (1);
    av += optindex;
    ac -= optindex;

    if (ac == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!optparse_getopt (p, "rank", &ranks))
        ranks = "all";

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");

    if (streq (ranks, "all")) {
        if (!(idset = idset_create (size, 0)))
            log_err_exit ("idset_create");
        if (idset_range_set (idset, 0, size - 1) < 0)
            log_err_exit ("idset_range_set");
    }
    else if (!(idset = idset_decode (ranks)))
        log_err_exit ("idset_decode (%s)", ranks);

    if (!(exec = bulk_exec_create (&ops, "rexec", 1234, "shell", h)))
        log_err_exit ("bulk_exec_create");

    if (bulk_exec_set_max_per_loop (exec, optparse_get_int (p, "mpl", -1)) < 0)
        log_err_exit ("bulk_exec_set_max_per_loop");

    ncmds = optparse_get_int (p, "ncmds", 1);

    push_commands (exec, idset, ncmds, ac, av);

    if (bulk_exec_start (h, exec) < 0)
        log_err_exit ("bulk_exec_start");

    flux_watcher_t *w = flux_signal_watcher_create (flux_get_reactor (h),
                                                    SIGINT,
                                                    signal_cb,
                                                    exec);
    flux_watcher_start (w);

    flux_watcher_t *cw = NULL;
    if ((cancel_after = optparse_get_int (p, "cancel-after", 0)) > 0) {
        cw = flux_check_watcher_create (flux_get_reactor (h),
                                        check_cancel_cb,
                                        exec);
        flux_watcher_start (cw);
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_watcher_destroy (w);
    flux_watcher_destroy (cw);
    idset_destroy (idset);
    bulk_exec_destroy (exec);
    flux_close (h);
    optparse_destroy (p);

    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
