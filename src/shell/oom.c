/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* oom.c - log kernel oom kill events
 *
 * This is an no-op if the cgroup v2 memory controller is not set up.
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
#include "ccan/str/str.h"

#include "builtins.h"
#include "internal.h"

struct shell_oom {
    flux_shell_t *shell;
    char *memory_events_path;
    int inotify_fd;
    int watch_id;
    flux_watcher_t *w;
    unsigned long oom_kill;
};

/* Read the contents of 'path' into NULL terminated buffer.
 * Caller must free.
 */
static char *read_file (const char *path)
{
    int fd;
    char *buf;
    if ((fd = open (path, O_RDONLY)) < 0)
        return NULL;
    if (read_all (fd, (void **)&buf) < 0) {
        ERRNO_SAFE_WRAP (close, fd);
        return NULL;
    }
    close (fd);
    return buf;
}

/* Determine the cgroup v2 path to 'name' for for pid.
 * Check access(2) to the file according to 'mode' mask.
 * Caller must free.
 */
static char *get_cgroup_path (pid_t pid, const char *name, int mode)
{
    char tmp[1024];
    char *cg;
    char *path;

    snprintf (tmp, sizeof (tmp), "/proc/%d/cgroup", (int)pid);
    if (!(cg = read_file (tmp)))
        return NULL;
    if (!strstarts (cg, "0::")) // v2 always begins with 0::
        goto eproto;
    snprintf (tmp,
              sizeof (tmp),
              "/sys/fs/cgroup/%s/%s",
              strstrip (cg + 3),
              name);
    if (!(path = realpath (tmp, NULL)))
        goto error;
    if (access (path, mode) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        goto error;
    }
    free (cg);
    return path;
eproto:
    errno = EPROTO;
error:
    ERRNO_SAFE_WRAP (free, cg);
    return NULL;
}

static int get_cgroup_value (const char *name, char *buf, size_t len)
{
    char *path;
    char *s = NULL;
    int rc = -1;

    if (!(path = get_cgroup_path (getpid (), name, R_OK))
        || !(s = read_file (path)))
        goto out;
    if (snprintf (buf, len, "%s", strstrip (s)) >= len)
        goto out;
    rc = 0;
out:
    free (s);
    free (path);
    return rc;
}

static const char *get_cgroup_size (const char *name)
{
    uint64_t size;
    char rawbuf[32];

    if (get_cgroup_value (name, rawbuf, sizeof (rawbuf)) < 0
        || parse_size (rawbuf, &size) < 0)
        return "unknown";
    return encode_size (size);
}

/* Parse 'name' from memory.events file.  Example content:
 *   low 0
 *   high 0
 *   max 0
 *   oom 0
 *   oom_kill 0
 *   oom_group_kill 0
 */
static int parse_memory_events (const char *s,
                                const char *name,
                                unsigned long *valp)
{
    char *argz = NULL;
    size_t argz_len = 0;
    int e;

    if ((e = argz_create_sep (s, '\n', &argz, &argz_len)) != 0) {
        errno = e;
        return -1;
    }
    char *entry = NULL;
    while ((entry = argz_next (argz, argz_len, entry))) {
        if (strstarts (entry, name) && isblank (entry[strlen (name)])) {
            unsigned long val;
            char *endptr;
            errno = 0;
            val = strtoul (&entry[strlen (name) + 1], &endptr, 10);
            if (errno == 0 && *endptr == '\0') {
                *valp = val;
                free (argz);
                return 0;
            }
        }
    }
    free (argz);
    errno = EPROTO;
    return -1;
}

static void watch_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct shell_oom *oom = arg;
    char evbuf[sizeof (struct inotify_event) + NAME_MAX + 1];
    char *me;
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
    if (!(me = read_file (oom->memory_events_path))
        || parse_memory_events (me, "oom_kill", &count) < 0) {
        shell_log_error ("error reading %s", oom->memory_events_path);
        goto out;
    }
    /* If any new oom events have been recorded, log them.
     */
    if (oom->oom_kill < count) {
        shell_log_error ("Memory cgroup out of memory: "
                         "killed %lu task%s on %s.",
                         count - oom->oom_kill,
                         count - oom->oom_kill > 1 ? "s" : "",
                         oom->shell->hostname);

        shell_log_error ("memory.peak = %s", get_cgroup_size ("memory.peak"));

        oom->oom_kill = count;
    }
out:
    free (me);
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
        free (oom->memory_events_path);
        free (oom);
        errno = saved_errno;
    }
}

static struct shell_oom *oom_create (flux_shell_t *shell,
                                     char *path,
                                     flux_error_t *errp)
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
    oom->memory_events_path = path; // takes ownership
    return oom;
error:
    oom_destroy (oom);
    return NULL;
}

static int oom_init (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *arg,
                     void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_oom *oom;
    flux_error_t error;
    char *path;

    // assume cgroup is not configured or not v2
    if (!(path = get_cgroup_path (getpid (), "memory.events", R_OK)))
        return 0;
    if (!(oom = oom_create (shell, path, &error))) {
        ERRNO_SAFE_WRAP (free, path);
        shell_warn ("disabling oom detection: %s", error.text);
        return 0;
    }
    if (flux_plugin_aux_set (p, "oom", oom, (flux_free_f)oom_destroy) < 0) {
        oom_destroy (oom);
        return -1;
    }
    shell_debug ("monitoring %s", path);
    return 0;
}

struct shell_builtin builtin_oom = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = oom_init,
};

// vi:ts=4 sw=4 expandtab
