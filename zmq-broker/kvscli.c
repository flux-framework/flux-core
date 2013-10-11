/* kvscli.c - kvs client code used in plugin and api context */

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
#include "cmb.h"
#include "util.h"


struct kvsdir_struct {
    void *handle;
    char *key;
    json_object *o;
};

struct kvsdir_iterator_struct {
    kvsdir_t dir;
    struct json_object_iterator next;
    struct json_object_iterator end;
};

static struct {
    RequestFun *request;
    BarrierFun *barrier;
} kvs_config;

void kvsdir_destroy (kvsdir_t dir)
{
    free (dir->key);
    json_object_put (dir->o);
    free (dir);
}

void kvs_reqfun_set (RequestFun *fun)
{
    kvs_config.request = fun;
}

void kvs_barrierfun_set (BarrierFun *fun)
{
    kvs_config.barrier = fun;
}

static kvsdir_t kvsdir_create (void *handle, const char *key, json_object *o)
{
    kvsdir_t dir = xzmalloc (sizeof (*dir));

    dir->handle = handle;
    dir->key = xstrdup (key);
    dir->o = o;
    json_object_get (dir->o);

    return dir;
}

const char *kvsdir_key (kvsdir_t dir)
{
    return dir->key;
}

void kvsitr_rewind (kvsitr_t itr)
{
    itr->next = json_object_iter_begin (itr->dir->o); 
}

kvsitr_t kvsitr_create (kvsdir_t dir)
{
    kvsitr_t itr = xzmalloc (sizeof (*itr));

    itr->dir = dir;
    itr->next = json_object_iter_begin (itr->dir->o); 
    itr->end = json_object_iter_end (itr->dir->o); 

    return itr;
}

void kvsitr_destroy (kvsitr_t itr)
{
    free (itr); 
}

const char *kvsitr_next (kvsitr_t itr)
{
    const char *name = NULL;

    if (!json_object_iter_equal (&itr->end, &itr->next)) {
        name = json_object_iter_peek_name (&itr->next);
        (void)json_object_iter_next (&itr->next);
    }

    return name;
}

int kvs_get (void *h, const char *key, json_object **valp)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    json_object_object_add (request, key, NULL);
    reply = kvs_config.request (h, request, "kvs.get");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errno) == 0)
        goto done;
    if (!(val = json_object_object_get (reply, key))) {
        errno = ENOENT;
        goto done;
    }
    json_object_get (val);
    *valp = val;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_get_dir (void *h, const char *key, kvsdir_t *dirp)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    util_json_object_add_boolean (request, ".flag_directory", true);
    json_object_object_add (request, key, NULL);
    reply = kvs_config.request (h, request, "kvs.get");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errno) == 0)
        goto done;
    if (!(val = json_object_object_get (reply, key))) {
        errno = ENOENT;
        goto done;
    }
    *dirp = kvsdir_create (h, key, val);
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_get_string (void *h, const char *key, char **valp)
{
    json_object *o = NULL;
    const char *s;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    s = json_object_get_string (o);
    *valp = xstrdup (s);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_int (void *h, const char *key, int *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_int (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_int64 (void *h, const char *key, int64_t *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_int64 (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_double (void *h, const char *key, double *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_double) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_double (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_boolean (void *h, const char *key, bool *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_boolean) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_boolean (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

bool kvsdir_exists (kvsdir_t dir, const char *name)
{
    bool rc = false;

    if (json_object_object_get (dir->o, name))
        rc = true;

    return rc;
}

bool kvsdir_isdir (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);
    bool rc = false;

    if (dirent && (json_object_object_get (dirent, "DIRREF") ||
                   json_object_object_get (dirent, "DIRVAL")))
        rc = true;

    return rc;
}

bool kvsdir_isstring (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);
    json_object *val;
    bool rc = false;

    if (dirent && (val = json_object_object_get (dirent, "FILEVAL"))
               && json_object_get_type (val) == json_type_string)
        rc = true;

    return rc;
}

bool kvsdir_isint (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);
    json_object *val;
    bool rc = false;

    if (dirent && (val = json_object_object_get (dirent, "FILEVAL"))
               && json_object_get_type (val) == json_type_int)
        rc = true;

    return rc;
}

bool kvsdir_isint64 (kvsdir_t dir, const char *name)
{
    return kvsdir_isint (dir, name); /* no way to distinguish from int */
}

bool kvsdir_isdouble (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);
    json_object *val;
    bool rc = false;

    if (dirent && (val = json_object_object_get (dirent, "FILEVAL"))
               && json_object_get_type (val) == json_type_double)
        rc = true;

    return rc;
}

bool kvsdir_isboolean (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);
    json_object *val;
    bool rc = false;

    if (dirent && (val = json_object_object_get (dirent, "FILEVAL"))
               && json_object_get_type (val) == json_type_boolean)
        rc = true;

    return rc;
}

char *kvsdir_key_at (kvsdir_t dir, const char *name)
{
    char *key;

    if (!strcmp (dir->key, ".") != 0)
        key = xstrdup (name);
    else if (asprintf (&key, "%s.%s", dir->key, name) < 0)
        oom ();
    return key;
}

int kvs_get_at (kvsdir_t dir, const char *name, json_object **valp)
{
    json_object *dirent;
    int rc = -1;

    if (!(dirent = json_object_object_get (dir->o, name))) {
        errno = ENOENT;
        goto done;
    }
    if (!(*valp = json_object_object_get (dirent, "FILEVAL"))) {
        errno = EISDIR;
        goto done;
    }
    json_object_get (*valp);
    rc = 0;
done:
    return rc;
}

int kvs_get_dir_at (kvsdir_t dir, const char *name, kvsdir_t *ndir)
{
    char *key;
    int rc = -1;

    if (!json_object_object_get (dir->o, name)) {
        errno = ENOENT;
        goto done;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_get_dir (dir->handle, key, ndir);
    free (key);
done:
    return rc;
}

int kvs_get_string_at (kvsdir_t dir, const char *name, char **valp)
{
    json_object *o;
    const char *s;
    int rc = -1;

    if (kvs_get_at (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    s = json_object_get_string (o);
    *valp = xstrdup (s);
    rc = 0;
done:
    return rc;
}

int kvs_get_int_at (kvsdir_t dir, const char *name, int *valp)
{
    json_object *o;
    int rc = -1;

    if (kvs_get_at (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_int (o);
    rc = 0;
done:
    return rc;
}

int kvs_get_int64_at (kvsdir_t dir, const char *name, int64_t *valp)
{
    json_object *o;
    int rc = -1;

    if (kvs_get_at (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_int64 (o);
    rc = 0;
done:
    return rc;
}

int kvs_get_double_at (kvsdir_t dir, const char *name, double *valp)
{
    json_object *o;
    int rc = -1;

    if (kvs_get_at (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_double) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_double (o);
    rc = 0;
done:
    return rc;
}

int kvs_get_boolean_at (kvsdir_t dir, const char *name, bool *valp)
{
    json_object *o;
    int rc = -1;

    if (kvs_get_at (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_boolean) {
        errno = EINVAL;
        goto done;
    }
    *valp = json_object_get_boolean (o);
    rc = 0;
done:
    return rc;
}

int kvs_put (void *h, const char *key, json_object *val)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum;
    int ret = -1;

    if (val)
        json_object_get (val);
    json_object_object_add (request, key, val);
    reply = kvs_config.request (h, request, "kvs.put");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errnum) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_put_string (void *h, const char *key, const char *val)
{
    json_object *o = NULL;
    int rc = -1;

    if (val && !(o = json_object_new_string (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_int (void *h, const char *key, int val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_int (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_int64 (void *h, const char *key, int64_t val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_int64 (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_double (void *h, const char *key, double val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_double (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_boolean (void *h, const char *key, bool val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_boolean (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_unlink (void *h, const char *key)
{
    return kvs_put (h, key, NULL);
}

int kvs_mkdir (void *h, const char *key)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum = 0;
    int ret = -1;
  
    util_json_object_add_boolean (request, ".flag_mkdir", true);
    json_object_object_add (request, key, NULL); 
    reply = kvs_config.request (h, request, "kvs.put");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errnum) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

/* helper for cmb_kvs_commit, cmb_kvs_fence */
static int send_kvs_flush (void *h)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum = 0;
    int ret = -1;
   
    reply = kvs_config.request (h, request, "kvs.flush");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errnum) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

/* helper for cmb_kvs_commit, cmb_kvs_fence */
static int send_kvs_commit (void *h, const char *name)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    char *uuid = NULL;
    int errnum = 0;
    int ret = -1;
  
    if (!name)
        uuid = uuid_generate_str ();
    util_json_object_add_string (request, "name", name ? name : uuid); 
    reply = kvs_config.request (h, request, "kvs.commit");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errnum) == 0) {
        errno = errnum;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    if (uuid)
        free (uuid);
    return ret;
}

int kvs_commit (void *h)
{
    if (send_kvs_flush (h) < 0)
        return -1;
    if (send_kvs_commit (h, NULL) < 0)
        return -1;
    return 0;
}

int kvs_fence (void *h, const char *name, int nprocs)
{
    if (send_kvs_flush (h) < 0)
        return -1;
    if (kvs_config.barrier (h, name, nprocs) < 0)
        return -1;
    if (send_kvs_commit (h, name) < 0)
        return -1;
    return 0;
}

int kvs_dropcache (void *h)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum = 0;
    int ret = -1;
  
    reply = kvs_config.request (h, request, "kvs.clean");
    if (!reply) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_int (reply, "errnum", &errnum) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
