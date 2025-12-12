/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* oom.c - adjust task oom score and log kernel oom kill events
 *
 * Monitoring is disabled if the cgroup v2 memory controller is not set up.
 */

#define FLUX_SHELL_PLUGIN_NAME "oom"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/strstrip.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libutil/cgroup.h"
#include "ccan/str/str.h"
#include "ccan/ptrint/ptrint.h"

#include "builtins.h"
#include "internal.h"

struct shell_oom {
    flux_shell_t *shell;
    struct cgroup_info cgroup;
    int inotify_fd;
    int watch_id;
    flux_watcher_t *w;
    unsigned long oom_kill;
};

static void watch_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct shell_oom *oom = arg;
    char evbuf[sizeof (struct inotify_event) + NAME_MAX + 1];
    unsigned long count;

    /* Consume an event from inotify file descriptor.
     * Ignore event contents since there is only one watch registered.
     */
    if (read (oom->inotify_fd, &evbuf, sizeof (evbuf)) < 0) {
        shell_log_error ("error reading from inotify fd");
        return;
    }
    /* Read memory.events.
     */
    if (cgroup_key_scanf (&oom->cgroup,
                          "memory.events",
                          "oom_kill",
                          "%lu", &count) != 1) {
        shell_log_error ("error reading memory.events");
        return;
    }
    /* If any new oom events have been recorded, log them.
     */
    if (oom->oom_kill < count) {
        shell_log_error ("Memory cgroup out of memory: "
                         "killed %lu task%s on %s.",
                         count - oom->oom_kill,
                         count - oom->oom_kill > 1 ? "s" : "",
                         oom->shell->hostname);

        unsigned long long peak;
        if (cgroup_scanf (&oom->cgroup, "memory.peak", "%llu", &peak) == 1)
            shell_log_error ("memory.peak = %s", encode_size (peak));

        oom->oom_kill = count;
    }
}

static void oom_destroy (struct shell_oom *oom)
{
    if (oom) {
        int saved_errno = errno;
        flux_watcher_destroy (oom->w);
        if (oom->watch_id >= 0)
            inotify_rm_watch (oom->inotify_fd, oom->watch_id);
        if (oom->inotify_fd >= 0)
            close (oom->inotify_fd);
        free (oom);
        errno = saved_errno;
    }
}

static struct shell_oom *oom_create (flux_shell_t *shell, flux_error_t *errp)
{
    struct shell_oom *oom;

    if (!shell) {
        errprintf (errp, "plugin not initialized with shell");
        return NULL;
    }
    if (!(oom = calloc (1, sizeof (*oom)))) {
        errprintf (errp, "%s", strerror (errno));
        return NULL;
    }
    oom->inotify_fd = -1;
    oom->watch_id = -1;
    oom->shell = shell;
    if (cgroup_info_init (&oom->cgroup) < 0) {
        errprintf (errp, "incompatible cgroup configuration");
        goto error;
    }
    const char *path = cgroup_path_to (&oom->cgroup, "memory.events");
    if (access (path, R_OK) < 0) {
        errprintf (errp, "no memory.events");
        goto error;
    }
    if ((oom->inotify_fd = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC)) < 0
        || (oom->watch_id = inotify_add_watch (oom->inotify_fd,
                                               path,
                                               IN_MODIFY)) < 0) {
        if (errno == EMFILE)
            errprintf (errp,
                       "max number of user inotify instances has been reached");
        else
            errprintf (errp, "error setting up inotify: %s", strerror (errno));
        goto error;
    }
    if (!(oom->w = flux_fd_watcher_create (shell->r,
                                           oom->inotify_fd,
                                           FLUX_POLLIN,
                                           watch_cb,
                                           oom))) {
        errprintf (errp,
                   "error setting up inotify watcher: %s",
                   strerror (errno));
        goto error;
    }
    flux_watcher_start (oom->w);
    return oom;
error:
    oom_destroy (oom);
    return NULL;
}

static int peek_int (pid_t pid, const char *name, int *val)
{
    char path[1024];
    FILE *fp;
    int value;

    snprintf (path, sizeof (path), "/proc/%d/%s", pid, name);
    if (!(fp = fopen (path, "r")))
        return -1;
    if (fscanf (fp, "%d", &value) != 1) {
        ERRNO_SAFE_WRAP (fclose, fp);
        return -1;
    }
    (void)fclose (fp);
    *val = value;
    return 0;
}

static int poke_int (pid_t pid, const char *name, int val)
{
    char path[1024];
    FILE *fp;

    snprintf (path, sizeof (path), "/proc/%d/%s", pid, name);
    if (!(fp = fopen (path, "w")))
        return -1;
    if (fprintf (fp, "%d", val) < 0) {
        ERRNO_SAFE_WRAP (fclose, fp);
        return -1;
    }
    if (fclose (fp) < 0)
        return -1;
    return 0;
}

/* N.B. avoid causing the job to fail if something doesn't work here.
 * If there is a failure to adjust, log that at debug level.
 * The proc reads are only to fill in the debug log - rather than
 * complicate things, just report -9999 (an illegal but not crazy huge value)
 * in the unlikely event that the read fails.
 */
static int oom_adjust (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    int adjust = ptr2int (data);
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    pid_t pid;
    int old_adjust = -9999;
    int score = -9999;
    bool failed = false;
    int saved_errno;

    if (!(shell = flux_plugin_get_shell (p))
        || !(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:I}", "pid", &pid) < 0)
        return shell_log_errno ("failed to get current task pid");

    (void)peek_int (pid, "oom_score_adj", &old_adjust);
    if (poke_int (pid, "oom_score_adj", adjust) < 0) {
        saved_errno = errno;
        failed = true;
    }
    (void)peek_int (pid, "oom_score", &score);

    shell_debug ("pid %d score_adj %d->%d score %d%s%s",
                 pid,
                 old_adjust,
                 adjust,
                 score,
                 failed ? ": " : "",
                 failed ? strerror (saved_errno) : "");

    return 0;
}

static int oom_init (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *arg,
                     void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_oom *oom;
    flux_error_t error;
    json_t *adjust = NULL;

    /* Handle -o oom.adjust=VAL
     * If the option is specified, register a task.fork handler which pokes
     * /proc/pid/oom_score_adj for each task.  This works even if cgroup
     * memory event monitoring is disabled below.
     */
    if (flux_shell_getopt_unpack (shell,
                                  "oom",
                                  "{s?o !}",
                                  "adjust", &adjust) < 0)
        return -1;
    if (adjust) {
        if (flux_plugin_add_handler (p,
                                     "task.fork",
                                     oom_adjust,
                                     int2ptr (json_integer_value (adjust))) < 0)
            return shell_log_errno ("error adding task.fork handler");
    }

    if (!(oom = oom_create (shell, &error))) {
        shell_debug ("disabling oom detection: %s", error.text);
        return 0;
    }
    if (flux_plugin_aux_set (p, "oom", oom, (flux_free_f)oom_destroy) < 0) {
        oom_destroy (oom);
        return -1;
    }
    shell_debug ("monitoring %s",
                 cgroup_path_to (&oom->cgroup, "memory.events"));
    return 0;
}

struct shell_builtin builtin_oom = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = oom_init,
};

// vi:ts=4 sw=4 expandtab
