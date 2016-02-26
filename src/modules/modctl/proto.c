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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "proto.h"

JSON modctl_tunload_enc (const char *name)
{
    JSON o = NULL;

    if (!name) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "name", name);
done:
    return o;
}

int modctl_tunload_dec (JSON o, const char **name)
{
    int rc = -1;

    if (!o || !name) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "name", name)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_runload_enc (int errnum)
{
    JSON o = NULL;

    o = Jnew ();
    Jadd_int (o, "errnum", errnum);
    return o;
}

int modctl_runload_dec (JSON o, int *errnum)
{
    int rc = -1;

    if (!o || !errnum) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}


JSON modctl_tload_enc (const char *path, int argc, char **argv)
{
    JSON args, o = NULL;
    int i;

    if (!path) {
        errno = EINVAL;
        goto done;
    }
    for (i = 0; i < argc; i++) {
        if (argv[i] == NULL) {
            errno = EINVAL;
            goto done;
        }
    }
    o = Jnew ();
    Jadd_str (o, "path", path);
    args = Jnew_ar ();
    for (i = 0; i < argc; i++)
        Jadd_ar_str (args, argv[i]);
    Jadd_obj (o, "args", args);
done:
    return o;
}

int modctl_tload_dec (JSON o, const char **path, int *argc, const char ***argv)
{
    JSON args = NULL;
    int i, ac, rc = -1;
    const char **av;

    if (!o || !path || !argc || !argv) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "path", path) || !Jget_obj (o, "args", &args)
                                    || !Jget_ar_len (args, &ac)) {
        errno = EPROTO;
        goto done;
    }
    av = xzmalloc (sizeof (av[0]) * ac);
    for (i = 0; i < ac; i++) {
        if (!Jget_ar_str (args, i, &av[i])) {
            free (av);
            errno = EPROTO;
            goto done;
        }
    }
    *argc = ac;
    *argv = av;
    rc = 0;
done:
    return rc;
}

JSON modctl_rload_enc (int errnum)
{
    JSON o = Jnew ();
    Jadd_int (o, "errnum", errnum);
    return o;
}

int modctl_rload_dec (JSON o, int *errnum)
{
    int rc = -1;

    if (!o || !errnum) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_tlist_enc (const char *svc)
{
    JSON o = NULL;

    if (!svc) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "service", svc);
done:
    return o;
}

int modctl_tlist_dec (JSON o, const char **svc)
{
    int rc = -1;

    if (!o || !svc) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "service", svc)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_rlist_enc (void)
{
    JSON mods, o = Jnew ();
    mods = Jnew_ar ();
    Jadd_obj (o, "modules", mods);
    return o;
}

int modctl_rlist_enc_add (JSON o, const char *name, int size,
                          const char *digest, int idle, int status)
{
    JSON mods, mod;
    int rc = -1;

    if (!o || !name || !digest || size < 0 || idle < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "modules", &mods)) { /* does not take ref on 'mods' */
        errno = EINVAL;
        goto done;
    }
    mod = Jnew ();
    Jadd_str (mod, "name", name);
    Jadd_int (mod, "size", size);
    Jadd_str (mod, "digest", digest);
    Jadd_int (mod, "idle", idle);
    Jadd_int (mod, "status", status);
    Jadd_ar_obj (mods, mod); /* takes ref on mod */
    Jput (mod);
    rc = 0;
done:
    return rc;
}

int modctl_rlist_enc_errnum (JSON o, int errnum)
{
    Jadd_int (o, "errnum", errnum);
    return 0;
}

int modctl_rlist_dec (JSON o, int *errnum, int *len)
{
    int rc = -1;
    JSON mods;

    if (!o || !errnum || !len) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum) || !Jget_obj (o, "modules", &mods)
                                        || !Jget_ar_len (mods, len)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int modctl_rlist_dec_nth (JSON o, int n, const char **name,
                          int *size, const char **digest, int *idle,
                          int *status)
{
    JSON el, mods;
    int rc = -1;
    int len;

    if (!o || !name || !size || !digest || !idle) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "modules", &mods) || !Jget_ar_len (mods, &len)) {
        errno = EPROTO;
        goto done;
    }
    if (n < 0 || n > len) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_ar_obj (mods, n, &el)) { /* does not take ref on el */
        errno = EPROTO;
        goto done;
    }
    if (!Jget_str (el, "name", name) || !Jget_int (el, "size", size)
        || !Jget_str (el, "digest", digest) || !Jget_int (el, "idle", idle)
        || !Jget_int (el, "status", status)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
