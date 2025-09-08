/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#ifdef HAVE_LINUX_MAGIC_H
#include <linux/magic.h>
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994 /* from linux/magic.h */
#endif
#ifndef CGROUP_SUPER_MAGIC
#define CGROUP_SUPER_MAGIC 0x27e0eb
#endif
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif
#include <signal.h>

#include "basename.h"
#include "errno_safe.h"
#include "ccan/str/str.h"

#include "cgroup.h"

const char *cgroup_path_to (struct cgroup_info *cgroup, const char *name)
{
    static __thread char buf[PATH_MAX + 1];
    size_t size = sizeof (buf);

    if (snprintf (buf, size, "%s/%s", cgroup->path, name) >= size) {
        errno = EOVERFLOW;
        return NULL;
    }
    return buf;
}

static int cgroup_vscanf (struct cgroup_info *cgroup,
                          const char *name,
                          const char *fmt,
                          va_list ap)
{
    const char *path;
    FILE *fp;
    int rc;

    if (!(path = cgroup_path_to (cgroup, name)) || !(fp = fopen (path, "r")))
        return -1;
    rc = vfscanf (fp, fmt, ap);
    fclose (fp);

    return rc;
}

int cgroup_scanf (struct cgroup_info *cgroup,
                  const char *name,
                  const char *fmt,
                  ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = cgroup_vscanf (cgroup, name, fmt, ap);
    va_end (ap);

    return rc;
}

static int cgroup_key_vscanf (struct cgroup_info *cgroup,
                              const char *name,
                              const char *key,
                              const char *fmt,
                              va_list ap)
{
    const char *path;
    FILE *fp;
    char *line = NULL;
    size_t size = 0;
    int n;
    int rc = -1;

    if (!(path = cgroup_path_to (cgroup, name)) || !(fp = fopen (path, "r")))
        return -1;
    while ((n = getline (&line, &size, fp)) >= 0) {
        int keylen = strlen (key);
        if (strstarts (line, key) && isblank (line[keylen])) {
            rc = vsscanf (line + keylen + 1, fmt, ap);
            goto done;
        }
    }
    errno = ENOENT;
done:
    ERRNO_SAFE_WRAP (free, line);
    ERRNO_SAFE_WRAP (fclose, fp);
    return rc;
}

int cgroup_key_scanf (struct cgroup_info *cgroup,
                      const char *name,
                      const char *key,
                      const char *fmt,
                      ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = cgroup_key_vscanf (cgroup, name, key, fmt, ap);
    va_end (ap);

    return rc;
}

static char *remove_leading_dotdot (char *relpath)
{
    while (strncmp (relpath, "/..", 3) == 0)
        relpath += 3;
    return relpath;
}

/*
 *  Look up the current cgroup relative path from /proc/self/cgroup.
 *
 *  If cgroup->unified is true, then look for the first entry where
 *   'subsys' is an empty string.
 *
 *  Otherwise, use the `name=systemd` line.
 *
 *  See NOTES: /proc/[pid]/cgroup in cgroups(7).
 */
static int cgroup_init_path (struct cgroup_info *cgroup)
{
    int rc = -1;
    int n;
    FILE *fp;
    size_t size = 0;
    char *line = NULL;
    int saved_errno;

    if (!(fp = fopen ("/proc/self/cgroup", "r")))
        return -1;

    while ((n = getline (&line, &size, fp)) >= 0) {
        char *nl;
        char *relpath = NULL;
        char *subsys = strchr (line, ':');
        if ((nl = strchr (line, '\n')))
            *nl = '\0';
        if (subsys == NULL
            || *(++subsys) == '\0'
            || !(relpath = strchr (subsys, ':')))
            continue;

        /* Nullify subsys, relpath is already nul-terminated at newline */
        *(relpath++) = '\0';

        /* Remove leading /.. in relpath. This could be due to cgroup
         * mounted in a container.
         */
        relpath = remove_leading_dotdot (relpath);

        /*  If unified cgroups are being used, then stop when we find
         *   subsys="". Otherwise stop at subsys="name=systemd":
         */
        if ((cgroup->unified && subsys[0] == '\0')
            || (!cgroup->unified && strcmp (subsys, "name=systemd") == 0)) {
            int len = sizeof (cgroup->path);
            if (snprintf (cgroup->path,
                          len,
                          "%s%s",
                          cgroup->mount_dir,
                          relpath) < len)
                rc = 0;
            break;
        }
    }
    if (rc < 0)
        errno = ENOENT;

    saved_errno = errno;
    free (line);
    fclose (fp);
    errno = saved_errno;
    return rc;
}

/*  Determine if this system is using the unified (v2) or legacy (v1)
 *   cgroups hierarchy (See https://systemd.io/CGROUP_DELEGATION/)
 *   and mount point for systemd managed cgroups.
 */
static int cgroup_init_mount_dir_and_type (struct cgroup_info *cg)
{
#ifndef __APPLE__
    struct statfs fs;

    /*  Assume unified unless we discover otherwise
     */
    cg->unified = true;

    /*  Check if either /sys/fs/cgroup or /sys/fs/cgroup/unified
     *   are mounted as type cgroup2. If so, use this as the mount dir
     *   (Note: these paths are guaranteed to fit in cg->mount_dir, so
     *    no need to check for truncation)
     */
    (void) strlcpy (cg->mount_dir, "/sys/fs/cgroup", sizeof (cg->mount_dir));
    if (statfs (cg->mount_dir, &fs) < 0)
        return -1;

    /* if cgroup2 fs mounted: unified hierarchy for all users of cgroupfs
     */
    if (fs.f_type == CGROUP2_SUPER_MAGIC)
        return 0;

    /*  O/w, check if cgroup2 unified hierarchy mounted at
     *   /sys/fs/cgroup/unified
     */
    (void) strlcpy (cg->mount_dir,
                    "/sys/fs/cgroup/unified",
                    sizeof (cg->mount_dir));
    if (statfs (cg->mount_dir, &fs) < 0)
        return -1;

    if (fs.f_type == CGROUP2_SUPER_MAGIC)
        return 0;

    /*  O/w, if /sys/fs/cgroup is mounted as tmpfs, we need to check
     *   for /sys/fs/cgroup/systemd mounted as cgroupfs (legacy).
     */
    if (fs.f_type == TMPFS_MAGIC) {

        (void) strlcpy (cg->mount_dir,
                        "/sys/fs/cgroup/systemd",
                        sizeof (cg->mount_dir));
        if (statfs (cg->mount_dir, &fs) == 0
            && fs.f_type == CGROUP_SUPER_MAGIC) {
            cg->unified = false;
            return 0;
        }
    }

#endif
    /*  Unable to determine cgroup mount point and/or unified vs legacy */
    return -1;
}

int cgroup_info_init (struct cgroup_info *cgroup)
{
    if (cgroup_init_mount_dir_and_type (cgroup) < 0
        || cgroup_init_path (cgroup) < 0)
        return -1;
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
