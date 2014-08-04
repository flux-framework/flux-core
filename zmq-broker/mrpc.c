/* mrpc.c - group rpc implementatino */ 

/* Group RPC event format:
 *    tag:  mrpc.<plugin>.<method>[.<method>]...
 *    JSON: path="mrpc.<uuid>"
 *          dest="nodeset"
 *          vers=N
 *          sender=N
 */

#define _GNU_SOURCE
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
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "nodeset.h"
#include "flux.h"
#include "kvs.h"

#define KVS_CLEANUP 1

struct flux_mrpc_struct {
    flux_t h;
    zuuid_t *uuid;
    char *path;
    char *dest;
    int nprocs;
    int sender;
    int vers;
    nodeset_t ns;
    nodeset_itr_t ns_itr;
    bool client;
};

static bool dest_valid (flux_mrpc_t f, int maxid)
{
    uint32_t r;
    int count = 0;

    nodeset_itr_rewind (f->ns_itr);
    while ((r = nodeset_next (f->ns_itr)) != NODESET_EOF) {
        count++;
        if (r > maxid)
            break;
    }
    if (count < 1 || r != NODESET_EOF)
        return false;
    return true;
}

flux_mrpc_t flux_mrpc_create (flux_t h, const char *dest)
{
    flux_mrpc_t f = xzmalloc (sizeof (*f));
    int maxid = flux_size (h) - 1;

    f->h = h;
    f->client = true;
    f->sender = flux_rank (h);
    f->dest = xstrdup (dest);
    if (!(f->ns = nodeset_new_str (dest))
        || !(f->ns_itr = nodeset_itr_new (f->ns)) || !dest_valid (f, maxid)) {
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

void flux_mrpc_destroy (flux_mrpc_t f)
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
        nodeset_itr_destroy (f->ns_itr);
    if (f->ns)
        nodeset_destroy (f->ns);
    if (f->dest)
        free (f->dest);
    free (f);
}

void flux_mrpc_put_inarg (flux_mrpc_t f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_mrpc_get_inarg (flux_mrpc_t f, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

void flux_mrpc_put_outarg (flux_mrpc_t f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.out-%d", f->path, flux_rank (f->h)) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_mrpc_get_outarg (flux_mrpc_t f, int nodeid, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.out-%d", f->path, nodeid) < 0)
        oom ();
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

int flux_mrpc_next_outarg (flux_mrpc_t f)
{
    uint32_t r = nodeset_next (f->ns_itr);
    return (r == NODESET_EOF ? -1 : r);
}

void flux_mrpc_rewind_outarg (flux_mrpc_t f)
{
    nodeset_itr_rewind (f->ns_itr);
}

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...)
{
    int rc = -1;
    char *tag = NULL;
    va_list ap;
    json_object *request = util_json_object_new_object ();

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (kvs_commit (f->h) < 0)
        goto done;
    if (kvs_get_version (f->h, &f->vers) < 0)
        goto done;
    util_json_object_add_string (request, "dest", f->dest);
    util_json_object_add_int (request, "vers", f->vers);
    util_json_object_add_int (request, "sender", f->sender);
    util_json_object_add_string (request, "path", f->path);
    if (flux_event_send (f->h, request, "mrpc.%s", tag) < 0)
        goto done;
    if (kvs_fence (f->h, f->path, f->nprocs + 1) < 0)
        goto done;
    rc = 0;
done:
    if (tag)
        free (tag);
    if (request)
        json_object_put (request);
    return rc;
}

flux_mrpc_t flux_mrpc_create_fromevent (flux_t h, json_object *request)
{
    flux_mrpc_t f = NULL;
    const char *dest, *path;
    int sender, vers;
    nodeset_t ns = NULL;

    if (util_json_object_get_string (request, "dest", &dest) < 0
            || util_json_object_get_string (request, "path", &path) < 0
            || util_json_object_get_int (request, "sender", &sender) < 0
            || util_json_object_get_int (request, "vers", &vers) < 0
            || !(ns = nodeset_new_str (dest))) {
        errno = EPROTO;
        goto done;
    }
    if (!nodeset_test_rank (ns, flux_rank (h))) {
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

int flux_mrpc_respond (flux_mrpc_t f)
{
    return kvs_fence (f->h, f->path, f->nprocs + 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
