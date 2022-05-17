/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define FLUX_SHELL_PLUGIN_NAME "tmpdir"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "src/common/libutil/cleanup.h"

#include "builtins.h"
#include "internal.h"
#include "info.h"

static int mkdir_exist_ok (const char *path, bool quiet)
{
    if (mkdir (path, 0700) < 0) {
        if (errno != EEXIST) {
            if (!quiet)
                shell_log_errno ("mkdir %s", path);
            return -1;
        }
    }
    return 0;
}

static int make_job_path (flux_shell_t *shell,
                          const char *parent,
                          char *buf,
                          size_t size)
{
    size_t n;

    n = snprintf (buf, size, "%s/jobtmp-%d-", parent, shell->info->shell_rank);
    if (n >= size) {
        errno = EOVERFLOW;
        return -1;
    }
    if (flux_job_id_encode (shell->jobid, "f58", buf + n, size - n) < 0)
        return -1;
    return 0;
}

static int mkjobtmp_rundir (flux_shell_t *shell, char *buf, size_t size)
{
    const char *rundir;

    if (shell->standalone
        || !(rundir = flux_attr_get (shell->h, "rundir"))
        || make_job_path (shell, rundir, buf, size) < 0
        || mkdir_exist_ok (buf, true) < 0)
        return -1;
    return 0;
}

static int mkjobtmp_tmpdir (flux_shell_t *shell, char *buf, size_t size)
{
    const char *tmpdir = flux_shell_getenv (shell, "TMPDIR");

    if (make_job_path (shell, tmpdir ? tmpdir : "/tmp", buf, size) < 0
        || mkdir_exist_ok (buf, false) < 0)
        return -1;
    return 0;
}

static int tmpdir_init (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    const char *tmpdir = flux_shell_getenv (shell, "TMPDIR");
    char jobtmp[1024];

    /*  Attempt to create TMPDIR if set. If this fails, fallback to /tmp.
     */
    if (tmpdir && mkdir_exist_ok (tmpdir, true) < 0) {
        shell_warn ("Unable to create TMPDIR=%s, resetting TMPDIR=/tmp",
                    tmpdir);
        tmpdir = "/tmp";
        if (flux_shell_setenvf (shell, 1, "TMPDIR", "%s", tmpdir) < 0)
            shell_die_errno (1, "Unable to set TMPDIR=/tmp");
    }

    /* Try to create jobtmp in broker rundir.
     * Fall back to ${TMPDIR:-/tmp} if that fails (e.g. guest user).
     */
    if (mkjobtmp_rundir (shell, jobtmp, sizeof (jobtmp)) < 0
        && mkjobtmp_tmpdir (shell, jobtmp, sizeof (jobtmp)) < 0)
        shell_die_errno (1, "error creating FLUX_JOB_TMPDIR");
    cleanup_push_string (cleanup_directory_recursive, jobtmp);

    /* Set/change FLUX_JOB_TMPDIR to jobtmp.
     * If TMPDIR is unset, set it to $FLUX_JOB_TMPDIR.
     */
    if (flux_shell_setenvf (shell, 1, "FLUX_JOB_TMPDIR", "%s", jobtmp) < 0
        || flux_shell_setenvf (shell, 0, "TMPDIR", "%s", jobtmp) < 0)
        shell_die_errno (1, "error updating job environment");

    return 0;
}

struct shell_builtin builtin_tmpdir = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = tmpdir_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
