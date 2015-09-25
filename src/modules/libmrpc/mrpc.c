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

/* Group RPC event format:
 *    topic: mrpc.<plugin>.<method>[.<method>]...
 *    JSON:  path="mrpc.<uuid>"
 *           dest="nodeset"
 *           vers=N
 *           sender=N
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

#include "mrpc.h"

#define KVS_CLEANUP 1

struct flux_mrpc_struct {
    flux_t h;
    zuuid_t *uuid;
    char *path;
    char *dest;
    int nprocs;
    uint32_t sender;
    int vers;
    nodeset_t *ns;
    nodeset_iterator_t *ns_itr;
    bool client;
};

flux_mrpc_t *flux_mrpc_create (flux_t h, const char *dest)
{
    flux_mrpc_t *f = xzmalloc (sizeof (*f));
    uint32_t size;
    int maxid;

    if (flux_get_size (h, &size) < 0)
        goto error;
    maxid = size - 1;

    f->h = h;
    f->client = true;
    if (flux_get_rank (h, &f->sender) < 0)
       goto error; 
    f->dest = xstrdup (dest);
    if (!(f->ns = nodeset_create_string (dest)) || nodeset_count (f->ns) == 0
                                          || nodeset_max (f->ns) > maxid) {
        errno = EINVAL;
        goto error;
    }
    f->nprocs = nodeset_count (f->ns);
    if (!(f->uuid = zuuid_new ()))
        oom ();
    if (asprintf (&f->path, "mrpc.%s", zuuid_str (f->uuid)) < 0)
        oom ();

    return f;
error:
    flux_mrpc_destroy (f);
    return NULL;
}

void flux_mrpc_destroy (flux_mrpc_t *f)
{
    if (f->path) {
#if KVS_CLEANUP
        if (f->client) {
            if (kvs_unlink (f->h, f->path) < 0)
                err ("kvs_unlink %s", f->path);
            if (kvs_commit (f->h) < 0)
                err ("kvs_commit");
        }
#endif
        free (f->path);
    }
    if (f->uuid)
        zuuid_destroy (&f->uuid);
    if (f->ns_itr)
        nodeset_iterator_destroy (f->ns_itr);
    if (f->ns)
        nodeset_destroy (f->ns);
    if (f->dest)
        free (f->dest);
    free (f);
}

void flux_mrpc_put_inarg_obj (flux_mrpc_t *f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    kvs_put_obj (f->h, key, val);
    free (key);
}

int flux_mrpc_put_inarg (flux_mrpc_t *f, const char *json_str)
{
    char *key = NULL;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (kvs_put (f->h, key, json_str) < 0)
        goto done;
    rc = 0;
done:
    if (key)
        free (key);
    return rc;
}

int flux_mrpc_get_inarg_obj (flux_mrpc_t *f, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    if (kvs_get_obj (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

int flux_mrpc_get_inarg (flux_mrpc_t *f, char **json_str)
{
    char *key = NULL;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (kvs_get (f->h, key, json_str) < 0)
        goto done;
    rc = 0;
done:
    if (key)
        free (key);
    return rc;
}

void flux_mrpc_put_outarg_obj (flux_mrpc_t *f, json_object *val)
{
    char *key;
    uint32_t rank = 0;

    (void)flux_get_rank (f->h, &rank);
    if (asprintf (&key, "%s.out-%" PRIu32, f->path, rank) < 0)
        oom ();
    kvs_put_obj (f->h, key, val);
    free (key);
}

int flux_mrpc_put_outarg (flux_mrpc_t *f, const char *json_str)
{
    char *key = NULL;
    uint32_t rank;
    int rc = -1;

    if (flux_get_rank (f->h, &rank) < 0)
        goto done;
    if (asprintf (&key, "%s.out-%" PRIu32, f->path, rank) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (kvs_put (f->h, key, json_str) < 0)
        goto done;
    rc = 0;
done:
    if (key)
        free (key);
    return rc;
}

int flux_mrpc_get_outarg_obj (flux_mrpc_t *f, int nodeid, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.out-%d", f->path, nodeid) < 0)
        oom ();
    if (kvs_get_obj (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

int flux_mrpc_get_outarg (flux_mrpc_t *f, int nodeid, char **json_str)
{
    char *key = NULL;
    int rc = -1;

    if (asprintf (&key, "%s.out-%d", f->path, nodeid) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (kvs_get (f->h, key, json_str) < 0)
        goto done;
    rc = 0;
done:
    if (key)
        free (key);
    return rc;
}

int flux_mrpc_next_outarg (flux_mrpc_t *f)
{
    uint32_t r = -1;

    if (!f->ns_itr)
        f->ns_itr = nodeset_iterator_create (f->ns);
    if (f->ns_itr)
        r = nodeset_next (f->ns_itr);
    if (r == NODESET_EOF)
        r = -1;
    return r;
}

void flux_mrpc_rewind_outarg (flux_mrpc_t *f)
{
    if (!f->ns_itr)
        f->ns_itr = nodeset_iterator_create (f->ns);
    else
        nodeset_iterator_rewind (f->ns_itr);
}

int flux_mrpc (flux_mrpc_t *f, const char *fmt, ...)
{
    int rc = -1;
    char *topic = NULL, *s = NULL;
    va_list ap;
    JSON o = NULL;
    zmsg_t *zmsg = NULL;

    va_start (ap, fmt);
    s = xvasprintf (fmt, ap);
    va_end (ap);

    if (kvs_commit (f->h) < 0)
        goto done;
    if (kvs_get_version (f->h, &f->vers) < 0)
        goto done;
    o = Jnew ();
    Jadd_str (o, "dest", f->dest);
    Jadd_int (o, "vers", f->vers);
    Jadd_int (o, "sender", f->sender);
    Jadd_str (o, "path", f->path);
    topic = xasprintf ("mrpc.%s", s);
    if (!(zmsg = flux_event_encode (topic, Jtostr (o))))
        goto done;
    if (flux_sendmsg (f->h, &zmsg) < 0)
        goto done;
    if (kvs_fence (f->h, f->path, f->nprocs + 1) < 0)
        goto done;
    rc = 0;
done:
    if (s)
        free (s);
    if (topic)
        free (topic);
    Jput (o);
    zmsg_destroy (&zmsg);
    return rc;
}

flux_mrpc_t *flux_mrpc_create_fromevent_obj (flux_t h, json_object *o)
{
    flux_mrpc_t *f = NULL;
    const char *dest, *path;
    int sender, vers;
    nodeset_t *ns = NULL;
    uint32_t rank;

    if (util_json_object_get_string (o, "dest", &dest) < 0
            || util_json_object_get_string (o, "path", &path) < 0
            || util_json_object_get_int (o, "sender", &sender) < 0
            || util_json_object_get_int (o, "vers", &vers) < 0
            || !(ns = nodeset_create_string (dest))) {
        errno = EPROTO;
        goto done;
    }
    if (flux_get_rank (h, &rank) < 0)
        goto done;
    if (!nodeset_test_rank (ns, rank)) {
        errno = EINVAL;
        goto done;
    }
    if (kvs_wait_version (h, vers) < 0)
        goto done;

    f = xzmalloc (sizeof (*f));
    f->h = h;
    f->client = false;
    f->nprocs = nodeset_count (ns);
    f->path = xstrdup (path);
    f->sender = sender;
    f->dest = xstrdup (dest);
    f->vers = vers;
    f->ns = ns;
    ns = NULL;
done:
    if (ns)
        nodeset_destroy (ns);
    return f;
}

flux_mrpc_t *flux_mrpc_create_fromevent (flux_t h, const char *json_str)
{
    json_object *o;
    flux_mrpc_t *mrpc = NULL;

    if (!(o = json_tokener_parse (json_str))) {
        errno = EPROTO;
        goto done;
    }
    mrpc = flux_mrpc_create_fromevent_obj (h, o);
done:
    if (o)
        json_object_put (o);
    return mrpc;
}

int flux_mrpc_respond (flux_mrpc_t *f)
{
    return kvs_fence (f->h, f->path, f->nprocs + 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
