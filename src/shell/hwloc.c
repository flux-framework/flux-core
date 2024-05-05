/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* hwloc options handler
 */
#define FLUX_SHELL_PLUGIN_NAME "hwloc"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libutil/read_all.h"
#include "src/common/librlist/rhwloc.h"
#include "ccan/str/str.h"

#include "builtins.h"

static int create_xmlfile (flux_shell_t *shell, int do_restrict)
{
    int fd = -1;
    char *xmlfile = NULL;
    char *restricted_xml = NULL;
    const char *hwloc_xml;
    const char *tmpdir;

    if (!(tmpdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR")))
        tmpdir = "/tmp";

    if (flux_shell_get_hwloc_xml (shell, &hwloc_xml) < 0) {
        shell_log_error ("failed to get shell hwloc xml");
        goto error;
    }
    if (do_restrict) {
        if (!(restricted_xml = rhwloc_topology_xml_restrict (hwloc_xml))) {
            shell_log_errno ("failed to restrict topology xml");
            goto error;
        }
        hwloc_xml = restricted_xml;
    }
    if (asprintf (&xmlfile, "%s/hwloc.xml", tmpdir) < 0) {
        shell_log_error ("asprintf HWLOC_XMLFILE failed");
        goto error;
    }
    if ((fd = open (xmlfile, O_CREAT|O_EXCL|O_WRONLY, 0640)) < 0) {
        shell_log_errno ("%s", xmlfile);
        goto error;
    }
    shell_debug ("Writing %ld bytes to HWLOC_XMLFILE=%s\n",
                 (long int) strlen (hwloc_xml),
                 xmlfile);
    if (write_all (fd, hwloc_xml, strlen (hwloc_xml)) < 0 || close (fd) < 0) {
        shell_log_errno ("failed to write HWLOC_XMLFILE");
        goto error;
    }
    if (flux_shell_setenvf (shell, 0, "HWLOC_XMLFILE", "%s", xmlfile) < 0) {
        shell_log_errno ("failed to set HWLOC_XMLFILE in job environment");
        goto error;
    }
    /*  Note: HWLOC_XMLFILE will be ignored if HWLOC_COMPONENTS is also set
     *  in the environment, so unset that variable here.
     */
    if (flux_shell_unsetenv (shell, "HWLOC_COMPONENTS") < 0
        && errno != ENOENT) {
        shell_log_errno ("failed to unset HWLOC_COMPONENTS");
        goto error;
    }
    return 0;
error:
    if (fd >= 0)
        close (fd);
    free (xmlfile);
    free (restricted_xml);
    return -1;
}

static int hwloc_post_init (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    int xmlfile = 0;
    int do_restrict = 0;

    if (flux_shell_getopt_unpack (shell,
                                  "hwloc",
                                  "{s?i s?i}",
                                    "xmlfile", &xmlfile,
                                    "restrict", &do_restrict) < 0)
        return shell_log_errno ("failed to unpack hwloc options");

    if (xmlfile && create_xmlfile (shell, do_restrict) < 0)
        return shell_log_errno ("failed to write HWLOC_XMLFILE");
    return 0;
}

struct shell_builtin builtin_hwloc = {
    .name      = FLUX_SHELL_PLUGIN_NAME,
    .post_init = hwloc_post_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
