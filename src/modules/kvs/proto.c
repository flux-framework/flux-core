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

/* The kvs protocol needs substantial revision and documentation in an RFC.
 * In particular, the original protocol was designed with requests and
 * responses arranged as dictionaries that could ship multiple key-value
 * pairs, with "flags" added to the dictionary as special keys that begin
 * with ".flag_".  In practice we only do one key-value per request now.
 * This way of arranging JSON objects does not lend itself to documentation
 * using JSON Content Rules, or to concise use of the json-c API, as one
 * must use an iterator to walk the keys of the dictionary.
 */

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

JSON kp_tget_enc (const char *key, bool dir, bool link)
{
    JSON o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    json_object_object_add (o, key, NULL);
    if (dir)
        Jadd_bool (o, ".flag_directory", true);
    if (link)
        Jadd_bool (o, ".flag_readlink", true);
done:
    return o;
}

int kp_tget_dec (JSON o, const char **key, bool *dir, bool *link)
{
    json_object_iter iter;
    int rc = -1;
    const char *k = NULL;

    if (!o || !dir || !link || !key) {
        errno = EINVAL;
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6))
            continue;
        if (k) {
            errno = EPROTO;
            goto done;
        }
        k = iter.key;
    }
    if (!k) {
        errno = EPROTO;
        goto done;
    }
    *key = k;
    *dir = false;
    (void)Jget_bool (o, ".flag_directory", dir);
    *link = false;
    (void)Jget_bool (o, ".flag_readlink", link);

    rc = 0;
done:
    return rc;
}

JSON kp_rget_enc (const char *key, JSON val)
{
    JSON o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    json_object_object_add (o, key, val);
done:
    return o;
}

int kp_rget_dec (JSON o, JSON *val)
{
    json_object_iter iter;
    int rc = -1;
    const char *k = NULL;
    JSON v = NULL;

    if (!o || !val) {
        errno = EINVAL;
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6))
            continue;
        if (k) {
            errno = EPROTO;
            goto done;
        }
        k = iter.key;
        v = iter.val;
    }
    if (!k) {
        errno = EPROTO;
        goto done;
    }
    if (!v) {
        errno = ENOENT;
        goto done;
    }
    *val = v;
    rc = 0;
done:
    return rc;
}

/* kvs.watch
 */

JSON kp_twatch_enc (const char *key, JSON val,
                    bool once, bool first, bool dir, bool link)
{
    JSON o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    json_object_object_add (o, key, val);
    if (once)
        Jadd_bool (o, ".flag_once", true);
    if (first)
        Jadd_bool (o, ".flag_first", true);
    if (dir)
        Jadd_bool (o, ".flag_directory", true);
    if (link)
        Jadd_bool (o, ".flag_readlink", true);
done:
    return o;
}

int kp_twatch_dec (JSON o, const char **key, JSON *val,
                   bool *once, bool *first, bool *dir, bool *link)
{
    json_object_iter iter;
    int rc = -1;
    const char *k = NULL;
    JSON v = NULL;

    if (!o || !key || !val || !once || !first || !dir || !link) {
        errno = EINVAL;
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6))
            continue;
        if (k) {
            errno = EPROTO;
            goto done;
        }
        k = iter.key;
        v = iter.val;
    }
    if (!k) {
        errno = EPROTO;
        goto done;
    }
    *key = k;
    *val = v;
    *once = false;
    (void)Jget_bool (o, ".flag_once", once);
    *first = false;
    (void)Jget_bool (o, ".flag_first", first);
    *dir = false;
    (void)Jget_bool (o, ".flag_directory", dir);
    *link = false;
    (void)Jget_bool (o, ".flag_readlink", link);
    rc = 0;
done:
    return rc;
}

JSON kp_rwatch_enc (const char *key, JSON val)
{
    JSON o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    json_object_object_add (o, key, val);
done:
    return o;
}

int kp_rwatch_dec (JSON o, JSON *val)
{
    json_object_iter iter;
    int rc = -1;
    const char *k = NULL;
    JSON v = NULL;

    if (!o || !val) {
        errno = EINVAL;
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6))
            continue;
        if (k) {
            errno = EPROTO;
            goto done;
        }
        k = iter.key;
        v = iter.val;
    }
    if (!k) {
        errno = EPROTO;
        goto done;
    }
    *val = v; /* don't convert NULL to ENOENT */
    rc = 0;
done:
    return rc;
}

/* kvs.unwatch
 */

JSON kp_tunwatch_enc (const char *key)
{
    JSON o = NULL;

    if (!key) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "key", key);
done:
    return o;
}

int kp_tunwatch_dec (JSON o, const char **key)
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
JSON kp_tfence_enc (const char *name, int nprocs, JSON ops)
{
    JSON o = Jnew ();
    JSON empty_ops = NULL;

    Jadd_str (o, "name", name);
    Jadd_int (o, "nprocs", nprocs);
    if (!ops)
        ops = empty_ops = Jnew_ar();
    Jadd_obj (o, "ops", ops); /* takes a ref on ops */
    Jput (empty_ops);
    return o;
}

int kp_tfence_dec (JSON o, const char **name, int *nprocs, JSON *ops)
{
    int rc = -1;

    if (!name || !nprocs || !ops) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "ops", ops) || !Jget_str (o, "name", name)
                                  || !Jget_int (o, "nprocs", nprocs)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/* kvs.getroot
 */

JSON kp_rgetroot_enc (int rootseq, const char *rootdir)
{
    JSON o = NULL;

    if (!rootdir) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "rootseq", rootseq);
    Jadd_str (o, "rootdir", rootdir);
done:
    return o;
}

int kp_rgetroot_dec (JSON o, int *rootseq, const char **rootdir)
{
    int rc = -1;

    if (!rootseq || !rootdir) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "rootseq", rootseq) || !Jget_str (o, "rootdir", rootdir)){
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/* kvs.setroot (event)
 */

JSON kp_tsetroot_enc (int rootseq, const char *rootdir, JSON root,
                      JSON names)
{
    JSON o = NULL;
    int n;

    if (!rootdir || !names || !Jget_ar_len (names, &n) || n < 1) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "rootseq", rootseq);
    Jadd_str (o, "rootdir", rootdir);
    Jadd_obj (o, "names", names);         /* takes a ref */
    if (root)
        Jadd_obj (o, "rootdirval", root); /* takes a ref */
done:
    return o;
}

int kp_tsetroot_dec (JSON o, int *rootseq, const char **rootdir,
                     JSON *root, JSON *names)
{
    int rc = -1;

    if (!o || !rootseq || !rootdir || !root || !names) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "rootseq", rootseq) || !Jget_str (o, "rootdir", rootdir)
                                          || !Jget_obj (o, "names", names)) {
        errno = EPROTO;
        goto done;
    }
    *root = NULL;
    (void)Jget_obj (o, "rootdirval", root);
    rc = 0;
done:
    return rc;
}

/* kvs.error (event)
 */

json_object *kp_terror_enc (json_object *names, int errnum)
{
    JSON o = NULL;
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

int kp_terror_dec (JSON o, json_object **names, int *errnum)
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
