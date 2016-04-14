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
#include <unistd.h>
#include <assert.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/zio.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

#include "attr.h"
#include "runlevel.h"

struct runlevel {
    int level;
    struct subprocess_manager *sm;
    struct subprocess *rc[4];
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
            if (r->rc[i])
                subprocess_destroy (r->rc[i]);
        }
        free (r);
    }
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

static int runlevel_start_subprocess (runlevel_t *r, int level)
{
    if (r->rc[level]) {
        if (subprocess_run (r->rc[level]) < 0)
            return -1;
    } else {
        if (r->cb)
            r->cb (r, r->level, 0, "Not configured", r->cb_arg);
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
                r->cb (r, r->level, 0, "Skipped mode=none", r->cb_arg);
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
static int subprocess_cb (struct subprocess *p, void *arg)
{
    runlevel_t *r = arg;
    int rc = subprocess_exit_code (p);
    const char *exit_string = subprocess_exit_string (p);

    assert (r->rc[r->level] == p);
    r->rc[r->level] = NULL;

    if (r->cb)
        r->cb (r, r->level, rc, exit_string, r->cb_arg);

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
    json_object *o = NULL;
    const char *name;
    int len;
    bool eof;
    char *s = NULL, *argz = NULL, *line = NULL;
    size_t argz_len;

    r = subprocess_get_context (p, "runlevel_t");
    assert (r != NULL);

    if (!r->io_cb)
        goto done;
    if (!(o = Jfromstr (json_str)) || !Jget_str (o, "name", &name))
        goto done;
    len = zio_json_decode (json_str, (void **)&s, &eof);
    if (len <= 0 || !s || !*s || s[len] != '\0')
        goto done;
    if (argz_create_sep (s, '\n', &argz, &argz_len) != 0)
        goto done;
    while ((line = argz_next (argz, argz_len, line)) && *line)
        r->io_cb (r, name, line, r->io_cb_arg);
done:
    if (s)
        free (s);
    if (argz)
        free (argz);
    Jput (o);
    return 0;
}

static void path_prepend (char **s1, const char *s2)
{
    char *p;

    if (!s2)
        ;
    else if (!*s1)
        *s1 = xstrdup (s2);
    else if ((p = strstr (*s1, s2))) {
        int s2_len = strlen (s2);
        memmove (p, p + s2_len, strlen (p + s2_len) + 1);
        if (*p == ':')
            memmove (p, p + 1, strlen (p + 1) + 1);
        path_prepend (s1, s2);
    } else {
        p = xasprintf ("%s:%s", s2, *s1);
        free (*s1);
        *s1 = p;
    }
}

int runlevel_set_rc (runlevel_t *r, int level, const char *command,
                     const char *local_uri, const char *library_path,
                     const char *pmi_library_path)
{
    struct subprocess *p = NULL;
    char *ldpath = NULL;
    const char *shell = getenv ("SHELL");
    if (!shell)
        shell = "/bin/bash";

    if (level < 1 || level > 3 || r->rc[level] != NULL || !r->sm) {
        errno = EINVAL;
        goto error;
    }

    if (!pmi_library_path)
        pmi_library_path = PMI_LIBRARY_PATH;

    path_prepend (&ldpath, getenv ("LD_LIBRARY_PATH"));
    path_prepend (&ldpath, PROGRAM_LIBRARY_PATH);
    path_prepend (&ldpath, library_path);

    if (!(p = subprocess_create (r->sm))
            || subprocess_set_callback (p, subprocess_cb, r) < 0
            || subprocess_argv_append (p, shell) < 0
            || (command && subprocess_argv_append (p, "-c") < 0)
            || (command && subprocess_argv_append (p, command) < 0)
            || subprocess_set_environ (p, environ) < 0
            || subprocess_unsetenv (p, "PMI_FD") < 0
            || subprocess_unsetenv (p, "PMI_RANK") < 0
            || subprocess_unsetenv (p, "PMI_SIZE") < 0
            || subprocess_setenv (p, "I_MPI_PMI_LIBRARY",
                                  pmi_library_path, 1) < 0
            || (local_uri && subprocess_setenv (p, "FLUX_URI",
                                                local_uri, 1) < 0)
            || (ldpath && subprocess_setenv (p, "LD_LIBRARY_PATH",
                                             ldpath, 1) < 0))
        goto error;
    if (level == 1 || level == 3) {
        if (subprocess_setenv (p, "FLUX_NODESET_MASK", r->nodeset, 1) < 0)
            goto error;
        if (subprocess_set_io_callback (p, subprocess_io_cb) < 0)
            goto error;
        if (subprocess_set_context (p, "runlevel_t", r) < 0)
            goto error;
    }
    if (ldpath)
        free (ldpath);
    r->rc[level] = p;
    return 0;
error:
    if (ldpath)
        free (ldpath);
    if (p)
        subprocess_destroy (p);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
