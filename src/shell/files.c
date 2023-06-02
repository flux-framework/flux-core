/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  jobspec "files" attribute handler
 */
#define FLUX_SHELL_PLUGIN_NAME "files"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libfilemap/filemap.h"

#include "builtins.h"

static void trace (void *arg,
                   json_t *fileref,
                   const char *path,
                   int mode,
                   int64_t size,
                   int64_t mtime,
                   int64_t ctime,
                   const char *encoding)
{
    shell_trace ("extracting file %s size=%ju mode=%04o",
                 path,
                 (uintmax_t) size,
                 mode);
}

static int extract_job_files (flux_t *h,
                              const char *dir,
                              json_t *files)
{
    int rc = -1;
    char *orig_dir;
    flux_error_t error;

    if (!(orig_dir = getcwd (NULL, 0)))
        return shell_log_errno ("getcwd");
    if (chdir (dir) < 0) {
        shell_log_errno ("chdir %s", dir);
        goto out;
    }
    if (filemap_extract (h, files, true, &error, trace, NULL) < 0) {
        shell_log_error ("%s", error.text);
        goto out;
    }
    rc = 0;
out:
    if (chdir (orig_dir) < 0)
        shell_die_errno (1, "failed to chdir back to %s", orig_dir);
    free (orig_dir);
    return rc;
}

static int files_init (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    flux_t *h;
    flux_shell_t *shell = flux_plugin_get_shell (p);
    json_t *files = NULL;
    const char *tmpdir;

    if (!shell || !(h = flux_shell_get_flux (shell)))
        return shell_log_errno ("unable to get shell or flux handle");

    if (!(tmpdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR")))
        return shell_log_errno ("flux_shell_getenv");

    if (flux_shell_info_unpack (shell,
                               "{s:{s:{s:{s?o}}}}",
                               "jobspec",
                                 "attributes",
                                   "system",
                                     "files", &files) < 0)
        return shell_log_errno ("failed to unpack jobspec");

    return extract_job_files (h, tmpdir, files);
}

struct shell_builtin builtin_files = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = files_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
