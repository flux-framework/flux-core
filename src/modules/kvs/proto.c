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

/* kvs.get
 */

json_object *kp_tget_enc (json_object *rootdir, const char *key, int flags)
{
    json_object *o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    if (rootdir)
        Jadd_obj (o, "rootdir", rootdir); /* takes a ref on rootdir */
    Jadd_str (o, "key", key);
    Jadd_int (o, "flags", flags);
done:
    return o;
}

int kp_tget_dec (json_object *o, json_object **rootdir, const char **key,
		 int *flags)
{
    int rc = -1;

    if (!o || !key || !flags) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "key", key) || !Jget_int (o, "flags", flags)) {
        errno = EPROTO;
        goto done;
    }
    if (rootdir)
        *rootdir = Jobj_get (o, "rootdir");
    rc = 0;
done:
    return rc;
}

json_object *kp_rget_enc (json_object *rootdir, json_object *val)
{
    json_object *o = NULL;

    o = Jnew ();
    json_object_object_add (o, "rootdir", rootdir);
    json_object_object_add (o, "val", val);
    return o;
}

int kp_rget_dec (json_object *o, json_object **rootdir, json_object **val)
{
    int rc = -1;
    json_object *v;

    if (!o || !(v = Jobj_get (o, "val"))) {
        errno = EINVAL;
        goto done;
    }
    if (val)
        *val = v;
    if (rootdir)
        *rootdir = Jobj_get (o, "rootdir");
    rc = 0;
done:
    return rc;
}

/* kvs.watch
 */

json_object *kp_twatch_enc (const char *key, json_object *val, int flags)
{
    json_object *o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "key", key);
    json_object_object_add (o, "val", val);
    Jadd_int (o, "flags", flags);
done:
    return o;
}

int kp_twatch_dec (json_object *o, const char **key, json_object **val,
		   int *flags)
{
    int rc = -1;

    if (!o || !key || !val) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "key", key) || !Jget_int (o, "flags", flags)) {
        errno = EPROTO;
        goto done;
    }
    *val = Jobj_get (o, "val"); /* may be NULL */
    rc = 0;
done:
    return rc;
}

json_object *kp_rwatch_enc (json_object *val)
{
    json_object *o = NULL;

    o = Jnew ();
    json_object_object_add (o, "val", val);
    return o;
}

int kp_rwatch_dec (json_object *o, json_object **val)
{
    int rc = -1;

    if (!o || !val) {
        errno = EINVAL;
        goto done;
    }
    *val = Jobj_get (o, "val"); /* may be NULL */
    rc = 0;
done:
    return rc;
}

/* kvs.unwatch
 */

json_object *kp_tunwatch_enc (const char *key)
{
    json_object *o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "key", key);
done:
    return o;
}

int kp_tunwatch_dec (json_object *o, const char **key)
{
    int rc = -1;

    if (!o || !key) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "key", key)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/* kvs.fence
 */
json_object *kp_tfence_enc (const char *name, int nprocs, int flags,
                            json_object *ops)
{
    json_object *o = Jnew ();
    json_object *empty_ops = NULL;

    Jadd_str (o, "name", name);
    Jadd_int (o, "nprocs", nprocs);
    Jadd_int (o, "flags", flags);
    if (!ops)
        ops = empty_ops = Jnew_ar();
    Jadd_obj (o, "ops", ops); /* takes a ref on ops */
    Jput (empty_ops);
    return o;
}

int kp_tfence_dec (json_object *o, const char **name, int *nprocs,
                   int *flags, json_object **ops)
{
    int rc = -1;

    if (!name || !nprocs || !flags || !ops ) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "ops", ops) || !Jget_str (o, "name", name)
                                  || !Jget_int (o, "flags", flags)
                                  || !Jget_int (o, "nprocs", nprocs)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/* kvs.error (event)
 */

json_object *kp_terror_enc (json_object *names, int errnum)
{
    json_object *o = NULL;
    int n;

    if (!names || !Jget_ar_len (names, &n) || n < 1 || errnum == 0) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_obj (o, "names", names);         /* takes a ref */
    Jadd_int (o, "errnum", errnum);
done:
    return o;
}

int kp_terror_dec (json_object *o, json_object **names, int *errnum)
{
    int rc = -1;
    if (!o || !names || !errnum) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "names", names) || !Jget_int (o, "errnum", errnum)) {
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
