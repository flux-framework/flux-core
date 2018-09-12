/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/


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

#include "src/common/subprocess/subprocess.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"

#include "runlevel.h"

struct level {
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    struct timespec start;
    double timeout;
    flux_watcher_t *timer;
};

struct runlevel {
    int level;
    flux_t *h;
    struct level rc[4];
    runlevel_cb_f cb;
    void *cb_arg;
    runlevel_io_cb_f io_cb;
    void *io_cb_arg;
    char nodeset[64];
    const char *mode;
};

runlevel_t *runlevel_create (void)
{
    runlevel_t *r = malloc (sizeof (*r));
    if (!r) {
        errno = ENOMEM;
        return NULL;
    }
    memset (r, 0, sizeof (*r));
    r->mode = "normal";
    return r;
}

void runlevel_destroy (runlevel_t *r)
{
    if (r) {
        int i;
        for (i = 0; i < 4; i++) {
            if (r->rc[i].p)
                flux_subprocess_destroy (r->rc[i].p);
            if (r->rc[i].cmd)
                flux_cmd_destroy (r->rc[i].cmd);
            flux_watcher_destroy (r->rc[i].timer);
        }
        free (r);
    }
}

void runlevel_set_flux (runlevel_t *r, flux_t *h)
{
    r->h = h;
}

static int runlevel_set_mode (runlevel_t *r, const char *val)
{
    if (!strcmp (val, "normal"))
        r->mode = "normal";
    if (!strcmp (val, "normal"))
        r->mode = "normal";
    else if (!strcmp (val, "none"))
        r->mode = "none";
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int runlevel_attr_get (const char *name, const char **val, void *arg)
{
    runlevel_t *r = arg;

    if (!strcmp (name, "init.run-level")) {
        static char s[16];
        snprintf (s, sizeof (s), "%d", runlevel_get_level (r));
        if (val)
            *val = s;
    } else if (!strcmp (name, "init.rc2_timeout")) {
        static char s[16];
        snprintf (s, sizeof (s), "%.1f", r->rc[2].timeout);
        if (val)
            *val = s;
    } else if (!strcmp (name, "init.mode")) {
        *val = r->mode;
    } else {
        errno = EINVAL;
        goto error;
    }
    return 0;
error:
    return -1;
}

static int runlevel_attr_set (const char *name, const char *val, void *arg)
{
    runlevel_t *r = arg;

    if (!strcmp (name, "init.mode")) {
        if (runlevel_set_mode (r, val) < 0)
            goto error;
    } else if (!strcmp (name, "init.rc2_timeout")) {
        if ((r->rc[2].timeout = strtod (val, NULL)) < 0.) {
            errno = EINVAL;
            goto error;
        }
    } else {
        errno = EINVAL;
        goto error;
    }
    return 0;
error:
    return -1;
}

int runlevel_register_attrs (runlevel_t *r, attr_t *attrs)
{
    const char *val;

    if (attr_add_active (attrs, "init.run-level",
                         FLUX_ATTRFLAG_READONLY,
                         runlevel_attr_get, NULL, r) < 0)
        return -1;

    if (attr_get (attrs, "init.mode", &val, NULL) == 0) {

        if (runlevel_set_mode (r, val) < 0
                || attr_delete (attrs, "init.mode", true) < 0)
            return -1;
    }
    if (attr_add_active (attrs, "init.mode", 0,
                         runlevel_attr_get, runlevel_attr_set, r) < 0)
        return -1;

    if (attr_get (attrs, "init.rc2_timeout", &val, NULL) == 0) {

        if ((r->rc[2].timeout = strtod (val, NULL)) < 0.
                || attr_delete (attrs, "init.rc2_timeout", true) < 0)
            return -1;
    }
    if (attr_add_active (attrs, "init.rc2_timeout", 0,
                         runlevel_attr_get, runlevel_attr_set, r) < 0)
        return -1;

    return 0;
}

void runlevel_set_size (runlevel_t *r, uint32_t size)
{
    int n;

    if (size > 1)
        n = snprintf (r->nodeset, sizeof (r->nodeset),
                      "[0-%" PRIu32 "]", size - 1);
    else
        n = snprintf (r->nodeset, sizeof (r->nodeset), "[0]");
    assert (n < sizeof (r->nodeset));
}

void runlevel_set_callback (runlevel_t *r, runlevel_cb_f cb, void *arg)
{
    r->cb = cb;
    r->cb_arg = arg;
}

void runlevel_set_io_callback (runlevel_t *r, runlevel_io_cb_f cb, void *arg)
{
    r->io_cb = cb;
    r->io_cb_arg = arg;
}

static void runlevel_timeout (flux_reactor_t *reactor, flux_watcher_t *w,
                              int revents, void *arg)
{
    runlevel_t *r = arg;
    flux_future_t *f;
    flux_log (r->h, LOG_ERR, "runlevel %d timeout, sending SIGTERM", r->level);
    if (!(f = flux_subprocess_kill (r->rc[r->level].p, SIGTERM)))
        flux_log_error (r->h, "flux_subprocess_kill");
    /* don't care about response */
    flux_future_destroy (f);
}

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static void completion_cb (flux_subprocess_t *p)
{
    runlevel_t *r = flux_subprocess_get_context (p, "runlevel");
    const char *exit_string = NULL;
    int rc;

    if ((rc = flux_subprocess_exit_code (p)) < 0) {
        /* bash standard, signals + 128 */
        if ((rc = flux_subprocess_signaled (p)) >= 0) {
            rc += 128;
            exit_string = strsignal (rc);
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

    flux_watcher_stop (r->rc[r->level].timer);

    if (r->cb) {
        double elapsed = monotime_since (r->rc[r->level].start) / 1000;
        r->cb (r, r->level, rc, elapsed, exit_string, r->cb_arg);
    }
    flux_subprocess_destroy (p);
}

static void io_cb (flux_subprocess_t *p, const char *stream)
{
    runlevel_t *r;
    const char *ptr;
    int lenp;

    r = flux_subprocess_get_context (p, "runlevel");

    assert (r);
    assert (r->level == 1 || r->level == 3);

    if (!(ptr = flux_subprocess_read_line (p, stream, &lenp))) {
        flux_log_error (r->h, "%s: flux_subprocess_read_line", __FUNCTION__);
        return;
    }

    if (!lenp) {
        if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
            flux_log_error (r->h, "%s: flux_subprocess_read", __FUNCTION__);
            return;
        }
    }

    if (lenp && r->io_cb)
        r->io_cb (r, stream, ptr, r->io_cb_arg);
}

static int runlevel_start_subprocess (runlevel_t *r, int level)
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
        flux_reactor_t *reactor = flux_get_reactor (r->h);
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
                             &ops)))
            goto error;

        if (flux_subprocess_set_context (p, "runlevel", r) < 0)
            goto error;

        monotime (&r->rc[level].start);
        if (r->rc[level].timeout > 0.) {
            flux_watcher_t *w;
            if (!(w = flux_timer_watcher_create (reactor,
                                                 r->rc[level].timeout, 0.,
                                                 runlevel_timeout, r)))
                goto error;
            flux_watcher_start (w);
            r->rc[level].timer = w;
            flux_log (r->h, LOG_INFO, "runlevel %d (%.1fs) timer started",
                      level, r->rc[level].timeout);
        }

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

int runlevel_set_level (runlevel_t *r, int level)
{
    if (level < 1 || level > 3 || level <= r->level) {
        errno = EINVAL;
        return -1;
    }
    if (!strcmp (r->mode, "normal")) {
        r->level = level;
        if (runlevel_start_subprocess (r, level) < 0)
            return -1;
    } else if (!strcmp (r->mode, "none")) {
        r->level = level;
        if (level == 2) {
            if (runlevel_start_subprocess (r, level) < 0)
                return -1;
        } else  {
            if (r->cb)
                r->cb (r, r->level, 0, 0., "Skipped mode=none", r->cb_arg);
        }
    }
    return 0;
}

int runlevel_get_level (runlevel_t *r)
{
    return r->level;
}

int runlevel_set_rc (runlevel_t *r, int level, const char *cmd_argz,
                     size_t cmd_argz_len, const char *local_uri)
{
    flux_subprocess_t *p = NULL;
    flux_cmd_t *cmd = NULL;
    const char *shell = getenv ("SHELL");
    if (!shell)
        shell = "/bin/bash";

    if (level < 1 || level > 3 || r->rc[level].p != NULL) {
        errno = EINVAL;
        goto error;
    }

    // Only wrap in a shell if there is only one argument
    bool shell_wrap = argz_count (cmd_argz, cmd_argz_len) < 2;
    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        goto error;
    if (shell_wrap || !cmd_argz) {
        if (flux_cmd_argv_append (cmd, shell) < 0)
            goto error;
    }
    if (shell_wrap) {
        if (cmd_argz && flux_cmd_argv_append (cmd, "-c") < 0)
            goto error;
    }
    if (cmd_argz) {
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
    if (local_uri && flux_cmd_setenvf (cmd, 1, "FLUX_URI", local_uri) < 0)
        goto error;
    if (level == 1 || level == 3) {
        if (flux_cmd_setenvf (cmd, 1, "FLUX_NODESET_MASK", r->nodeset) < 0)
            goto error;
    }
    r->rc[level].cmd = cmd;
    return 0;
error:
    if (p)
        flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
