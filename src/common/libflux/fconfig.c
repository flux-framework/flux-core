/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#include <sys/types.h>
#include <pwd.h>

#include "security.h" /* FLUX_DIRECTORY */

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#include "fconfig.h"

static char *flux_config_default_path (bool create_dir)
{
    struct passwd *pw = getpwuid (getuid ());
    char *config_file;
    char *config_dir;

    if (!pw || !pw->pw_dir || strlen (pw->pw_dir) == 0)
        msg_exit ("who are you?");
    config_dir = xasprintf ("%s/%s", pw->pw_dir, FLUX_DIRECTORY);
    config_file = xasprintf ("%s/config", config_dir);
    if (create_dir && mkdir (config_dir, 0700) < 0 && errno != EEXIST)
        err_exit ("%s", config_dir);
    free (config_dir);
    return config_file;
}

zconfig_t *flux_config_load (const char *path, bool must_exist)
{
    char *default_path = NULL;
    zconfig_t *z;

    if (!path)
        path = default_path = flux_config_default_path (false);
    if (access (path, R_OK) < 0) {
        if (must_exist)
            err_exit ("%s", path);
        else if (!(z = zconfig_new ("root", NULL)))
            oom ();
    } else if (!(z = zconfig_load (path)))
        msg_exit ("%s: parse error", path);
    if (default_path)
        free (default_path);
    return z;
}

void flux_config_save (const char *path, zconfig_t *z)
{
    char *default_path = NULL;

    zconfig_set_comment (z, NULL);
    zconfig_set_comment (z, " The format of this file is described in");
    zconfig_set_comment (z, "     http://rfc.zeromq.org/spec:4/ZPL");
    zconfig_set_comment (z, " NOTE: indents must be exactly 4 spaces");
    zconfig_set_comment (z, "");
    umask (022);

    if (!path)
        path = default_path = flux_config_default_path (true);
    if (zconfig_save (z, path) < 0)
        err_exit ("%s", path);
    if (default_path)
        free (default_path);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
