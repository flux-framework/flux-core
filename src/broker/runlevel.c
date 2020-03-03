/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <argz.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"

#include "runlevel.h"

struct level {
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    struct timespec start;
};

struct runlevel {
    int level;
    flux_t *h;
    struct level rc[4];
    runlevel_cb_f cb;
    void *cb_arg;
    runlevel_io_cb_f io_cb;
    void *io_cb_arg;
};

static int runlevel_attr_get (const char *name, const char **val, void *arg);

struct runlevel *runlevel_create (flux_t *h, attr_t *attrs)
{
    struct runlevel *r;

    if (!(r = calloc (1, sizeof (*r))))
        return NULL;
    r->h = h;
    if (attr_add_active (attrs,
                         "init.run-level",
                         FLUX_ATTRFLAG_READONLY,
                         runlevel_attr_get,
                         NULL,
                         r) < 0)
        goto error;
    return r;
error:
    runlevel_destroy (r);
    return NULL;
}

void runlevel_destroy (struct runlevel *r)
{
    if (r) {
        int saved_errno = errno;
        int i;
        for (i = 0; i < 4; i++) {
            if (r->rc[i].p)
                flux_subprocess_destroy (r->rc[i].p);
            if (r->rc[i].cmd)
                flux_cmd_destroy (r->rc[i].cmd);
        }
        free (r);
        errno = saved_errno;
    }
}

static int runlevel_attr_get (const char *name, const char **val, void *arg)
{
    struct runlevel *r = arg;

    if (!strcmp (name, "init.run-level")) {
        static char s[16];
        snprintf (s, sizeof (s), "%d", runlevel_get_level (r));
        if (val)
            *val = s;
    } else {
        errno = EINVAL;
        goto error;
    }
    return 0;
error:
    return -1;
}

void runlevel_set_callback (struct runlevel *r, runlevel_cb_f cb, void *arg)
{
    r->cb = cb;
    r->cb_arg = arg;
}

void runlevel_set_io_callback (struct runlevel *r, runlevel_io_cb_f cb, void *arg)
{
    r->io_cb = cb;
    r->io_cb_arg = arg;
}

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static void completion_cb (flux_subprocess_t *p)
{
    struct runlevel *r = flux_subprocess_aux_get (p, "runlevel");
    const char *exit_string = NULL;
    int rc;

    if ((rc = flux_subprocess_exit_code (p)) < 0) {
        /* bash standard, signals + 128 */
        if ((rc = flux_subprocess_signaled (p)) >= 0) {
            exit_string = strsignal (rc);
            rc += 128;
        }
    }
    else {
        if (rc)
            exit_string = "Exited with non-zero status";
        else
            exit_string = "Exited";
    }

    assert (r->rc[r->level].p == p);
    r->rc[r->level].p = NULL;

    if (r->cb) {
        double elapsed = monotime_since (r->rc[r->level].start) / 1000;
        r->cb (r, r->level, rc, elapsed, exit_string, r->cb_arg);
    }
    flux_subprocess_destroy (p);
}

static void io_cb (flux_subprocess_t *p, const char *stream)
{
    struct runlevel *r;
    const char *ptr;
    int lenp;

    r = flux_subprocess_aux_get (p, "runlevel");

    assert (r);
    assert (r->level == 1 || r->level == 3);

    if (!(ptr = flux_subprocess_getline (p, stream, &lenp))) {
        flux_log_error (r->h, "%s: flux_subprocess_getline", __FUNCTION__);
        return;
    }

    if (lenp && r->io_cb)
        r->io_cb (r, stream, ptr, r->io_cb_arg);
}

static int runlevel_start_subprocess (struct runlevel *r, int level)
{
    flux_subprocess_t *p = NULL;

    assert (r->h != NULL);

    if (r->rc[level].cmd) {
        flux_subprocess_ops_t ops = {
            .on_completion = completion_cb,
            .on_state_change = NULL,
            .on_channel_out = NULL,
            .on_stdout = NULL,
            .on_stderr = NULL,
        };
        int flags = 0;

        /* set alternate io callback for levels 1 and 3 */
        if (level == 1 || level == 3) {
            ops.on_stdout = io_cb;
            ops.on_stderr = io_cb;
        }
        else
            flags |= FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH;

        if (!(p = flux_exec (r->h,
                             flags,
                             r->rc[level].cmd,
                             &ops,
                             NULL)))
            goto error;

        if (flux_subprocess_aux_set (p, "runlevel", r, NULL) < 0)
            goto error;

        monotime (&r->rc[level].start);

        r->rc[level].p = p;
    } else {
        if (r->cb)
            r->cb (r, r->level, 0, 0., "Not configured", r->cb_arg);
    }
    return 0;

error:
    flux_subprocess_destroy (p);
    return -1;
}

int runlevel_set_level (struct runlevel *r, int level)
{
    if (level < 1 || level > 3 || level <= r->level) {
        errno = EINVAL;
        return -1;
    }
    r->level = level;
    if (runlevel_start_subprocess (r, level) < 0)
        return -1;
    return 0;
}

int runlevel_get_level (struct runlevel *r)
{
    return r->level;
}

int runlevel_set_rc (struct runlevel *r, int level, const char *cmd_argz,
                     size_t cmd_argz_len, const char *local_uri)
{
    flux_cmd_t *cmd = NULL;
    const char *shell = getenv ("SHELL");
    if (!shell)
        shell = "/bin/bash";

    if (level < 1 || level > 3 || r->rc[level].p != NULL) {
        errno = EINVAL;
        goto error;
    }
    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        goto error;

    // Run interactive shell if there are no arguments
    if (argz_count (cmd_argz, cmd_argz_len) == 0) {
        if (flux_cmd_argv_append (cmd, shell) < 0)
            goto error;
    }
    // Wrap in shell -c if there is only one argument
    else if (argz_count (cmd_argz, cmd_argz_len) == 1) {
        char *arg = argz_next (cmd_argz, cmd_argz_len, NULL);

        if (flux_cmd_argv_append (cmd, shell) < 0)
            goto error;
        if (flux_cmd_argv_append (cmd, "-c") < 0)
            goto error;
        if (flux_cmd_argv_append (cmd, arg) < 0)
            goto error;
    }
    else {
        char *arg = argz_next (cmd_argz, cmd_argz_len, NULL);
        while (arg) {
            if (flux_cmd_argv_append (cmd, arg) < 0)
                goto error;
            arg = argz_next (cmd_argz, cmd_argz_len, arg);
        }
    }
    flux_cmd_unsetenv (cmd, "PMI_FD");
    flux_cmd_unsetenv (cmd, "PMI_RANK");
    flux_cmd_unsetenv (cmd, "PMI_SIZE");
    if (local_uri && flux_cmd_setenvf (cmd, 1, "FLUX_URI",
                                       "%s", local_uri) < 0)
        goto error;
    r->rc[level].cmd = cmd;
    return 0;
error:
    flux_cmd_destroy (cmd);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
