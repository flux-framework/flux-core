/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* unshare.c - unshare CLONE_NEWUSER or requested namespaces
 *
 * Shell options
 *   unshare[=name,name,...]
 */
#define FLUX_SHELL_PLUGIN_NAME "unshare"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sched.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <argz.h>
#include <flux/core.h>
#include <jansson.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"

#include "builtins.h"

struct ns {
    const char *name;
    int flag;
};

static struct ns nsmap[] = {
    { "files",      CLONE_FILES },
    { "fs",         CLONE_FS },
#ifdef CLONE_NEWCGROUP
    { "cgroup",     CLONE_NEWCGROUP },
#endif
    { "ipc",        CLONE_NEWIPC },
    { "net",        CLONE_NEWNET },
    { "mount",      CLONE_NEWNS },
    { "pid",        CLONE_NEWPID },
    { "user",       CLONE_NEWUSER },
#ifdef CLONE_NEWTIME
    { "time",       CLONE_NEWTIME },
#endif
    { "uts",        CLONE_NEWUTS },
    { "sysvsem",    CLONE_SYSVSEM },
};

static int getflag (const char *name, int *flagp)
{
    for (int i = 0; i < ARRAY_SIZE (nsmap); i++) {
        if (name && streq (nsmap[i].name, name)) {
            *flagp = nsmap[i].flag;
            return 0;
        }
    }
    return -1;
}

static int parse_options (const char *s, int *flagsp, bool *maprootp)
{
    error_t e;
    char *argz = NULL;
    size_t argz_len = 0;
    bool maproot = false;
    const char *name;
    int flags = 0;

    e = argz_create_sep (s, ',', &argz, &argz_len);
    if (e != 0) {
        shell_log_error ("%s", strerror (e));
        return -1;
    }
    name = NULL;
    while ((name = argz_next (argz, argz_len, name))) {
        int newflag;
        if (streq (name, "maproot"))
            maproot = true;
        else {
            if (getflag (name, &newflag) < 0) {
                shell_log_error ("unknown unshare name: %s", name);
                free (argz);
                return -1;
            }
            flags |= newflag;
        }
    }
    *flagsp = flags;
    *maprootp = maproot;
    return 0;
}

static int write_file (const char *path, const char *s)
{
    int fd;
    ssize_t count;

    fd = open (path, O_WRONLY);
    if (fd < 0)
        return shell_log_errno ("error opening %s for writing", path);
    if ((count = write (fd, s, strlen (s))) < 0) {
        shell_log_errno ("error writing to %s", path);
        goto error;
    }
    if (count < strlen (s)) {
        shell_log_error ("short write to %s", path);
        goto error;
    }
    if (close (fd) < 0)
        return shell_log_errno ("error closing %s", path);
    return 0;
error:
    (void)close (fd);
    return -1;
}

static int write_idmap (const char *path, int from_id, int to_id)
{
    char buf[64];

    snprintf (buf, sizeof (buf), "%d %d 1\n", to_id, from_id);
    return write_file (path, buf);
}

static int unshare_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *arg,
                         void *data)
{
    flux_shell_t *shell;
    json_t *o;
    bool maproot = false;
    int flags = 0;
    int rc;
    int old_uid = getuid ();
    int old_gid = getgid ();

    if (!(shell = flux_plugin_get_shell (p)))
        return -1;
    if ((rc = flux_shell_getopt_unpack (shell, "unshare", "o", &o)) < 0)
        return shell_log_errno ("unshare option parse error");
    if (rc == 0)
        return 0;
    if (json_is_string (o)) {
        if (parse_options (json_string_value (o), &flags, &maproot) < 0)
            return -1;
    }
    if (flags == 0)
        flags |= CLONE_NEWUSER;
    if (unshare (flags) < 0)
        return shell_log_errno ("unshare system call");
    if (maproot) {
        /* Order is important here.  See user_namespaces(7).
         */
        if (write_idmap ("/proc/self/uid_map", old_uid, 0) < 0
            || write_file ("/proc/self/setgroups", "deny") < 0
            || write_idmap ("/proc/self/gid_map", old_gid, 0) < 0)
            return -1;
    }
    return 0;
}

struct shell_builtin builtin_unshare = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = unshare_init,
};

// vi:ts=4 sw=4 expandtab
