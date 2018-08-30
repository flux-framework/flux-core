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

#include "src/common/libsubprocess/zio.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"

#include "runlevel.h"

struct level {
    struct subprocess *subprocess;
    struct timespec start;
    double timeout;
    flux_watcher_t *timer;
};

struct runlevel {
    int level;
    struct subprocess_manager *sm;
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
            if (r->rc[i].subprocess)
                subprocess_destroy (r->rc[i].subprocess);
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

void runlevel_set_subprocess_manager (runlevel_t *r,
                                      struct subprocess_manager *sm)
{
    r->sm = sm;
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
    flux_log (r->h, LOG_ERR, "runlevel %d timeout, sending SIGTERM", r->level);
    subprocess_kill (r->rc[r->level].subprocess, SIGTERM);
}

static int runlevel_start_subprocess (runlevel_t *r, int level)
{
    if (r->rc[level].subprocess) {
        if (subprocess_run (r->rc[level].subprocess) < 0)
            return -1;
        monotime (&r->rc[level].start);
        if (r->rc[level].timeout > 0.) {
            assert (r->h != NULL);
            flux_reactor_t *reactor = flux_get_reactor (r->h);
            flux_watcher_t *w;
            if (!(w = flux_timer_watcher_create (reactor,
                                                 r->rc[level].timeout, 0.,
                                                 runlevel_timeout, r)))
                return -1;
            flux_watcher_start (w);
            r->rc[level].timer = w;
            flux_log (r->h, LOG_INFO, "runlevel %d (%.1fs) timer started",
                      level, r->rc[level].timeout);
        }
    } else {
        if (r->cb)
            r->cb (r, r->level, 0, 0., "Not configured", r->cb_arg);
    }
    return 0;
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

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static int subprocess_cb (struct subprocess *p)
{
    runlevel_t *r = subprocess_get_context (p, "runlevel");
    int rc = subprocess_exit_code (p);
    const char *exit_string = subprocess_exit_string (p);

    assert (r->rc[r->level].subprocess == p);
    r->rc[r->level].subprocess = NULL;

    flux_watcher_stop (r->rc[r->level].timer);

    if (r->cb) {
        double elapsed = monotime_since (r->rc[r->level].start) / 1000;
        r->cb (r, r->level, rc, elapsed, exit_string, r->cb_arg);
    }
    subprocess_destroy (p);

    return 0;
}

/* Note: return value of this function is ignored by libsubprocess.
 * Also: zio_json_decode() returns -1 on error, 0 on eof, strlen(s) on
 * success; caller must free 's'.
 */
static int subprocess_io_cb (struct subprocess *p, const char *json_str)
{
    runlevel_t *r;
    json_t *o = NULL;
    const char *name;
    int len;
    bool eof;
    char *s = NULL, *argz = NULL, *line = NULL;
    size_t argz_len;

    r = subprocess_get_context (p, "runlevel");
    assert (r != NULL);
    assert (r->level == 1|| r->level == 3);

    if (!r->io_cb)
        goto done;
    /* N.B. libsubprocess tacks "name" etc. onto zio-encoded JSON output
     */
    if (!(o = json_loads (json_str, 0, NULL)))
        goto done;
    if (json_unpack (o, "{s:s}", "name", &name) < 0)
        goto done;
    len = zio_json_decode (json_str, (void **)&s, &eof);
    if (len <= 0 || !s || !*s || s[len] != '\0')
        goto done;
    if (argz_create_sep (s, '\n', &argz, &argz_len) != 0)
        goto done;
    while ((line = argz_next (argz, argz_len, line)) && *line)
        r->io_cb (r, name, line, r->io_cb_arg);
done:
    free (s);
    free (argz);
    json_decref (o);
    return 0;
}

int runlevel_set_rc (runlevel_t *r, int level, const char *cmd_argz,
                     size_t cmd_argz_len, const char *local_uri)
{
    struct subprocess *p = NULL;
    const char *shell = getenv ("SHELL");
    if (!shell)
        shell = "/bin/bash";

    if (level < 1 || level > 3 || r->rc[level].subprocess != NULL || !r->sm) {
        errno = EINVAL;
        goto error;
    }

    // Only wrap in a shell if there is only one argument
    bool shell_wrap = argz_count (cmd_argz, cmd_argz_len) < 2;
    if ((p = subprocess_create (r->sm)) == NULL)
        goto error;
    if ((subprocess_set_context (p, "runlevel", r)) < 0)
        goto error;
    if ((subprocess_add_hook (p, SUBPROCESS_COMPLETE, subprocess_cb)) < 0)
        goto error;
    if (shell_wrap || !cmd_argz) {
        if ((subprocess_argv_append (p, shell)) < 0)
            goto error;
    }
    if (shell_wrap) {
        if (cmd_argz && subprocess_argv_append (p, "-c") < 0)
            goto error;
    }
    if (cmd_argz && subprocess_argv_append_argz (p, cmd_argz, cmd_argz_len) < 0)
        goto error;
    if (subprocess_set_environ (p, environ) < 0)
        goto error;
    if (subprocess_unsetenv (p, "PMI_FD") < 0)
        goto error;
    if (subprocess_unsetenv (p, "PMI_RANK") < 0)
        goto error;
    if (subprocess_unsetenv (p, "PMI_SIZE") < 0)
        goto error;
    if (local_uri && subprocess_setenv (p, "FLUX_URI", local_uri, 1) < 0)
        goto error;

    if (level == 1 || level == 3) {
        if (subprocess_setenv (p, "FLUX_NODESET_MASK", r->nodeset, 1) < 0)
            goto error;
        if (subprocess_set_io_callback (p, subprocess_io_cb) < 0)
            goto error;
    }
    r->rc[level].subprocess = p;
    return 0;
error:
    if (p)
        subprocess_destroy (p);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
