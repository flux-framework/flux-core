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
    int flags;
};

struct kvsdir_iterator_struct {
    kvsdir_t dir;
    struct json_object_iterator next;
    struct json_object_iterator end;
};

struct kvs_config_struct {
    KVSReqF *request;
    KVSBarrierF *barrier;
};
static struct kvs_config_struct kvs_config = {
    .request = NULL,
    .barrier = NULL
};

void kvsdir_destroy (kvsdir_t dir)
{
    free (dir->key);
    json_object_put (dir->o);
    free (dir);
}

static kvsdir_t kvsdir_alloc (void *handle, const char *key, json_object *o,
                              int flags)
{
    kvsdir_t dir = xzmalloc (sizeof (*dir));

    dir->handle = handle;
    dir->key = xstrdup (key);
    dir->o = o;
    json_object_get (dir->o);
    dir->flags = flags;

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
    assert (kvs_config.request != NULL);
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
    if (valp) {
        json_object_get (val);
        *valp = val;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_get_dir (void *h, const char *key, kvsdir_t *dirp, int flags)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    util_json_object_add_boolean (request, ".flag_directory", true);
    util_json_object_add_boolean (request, ".flag_fileval",
                                  (flags & KVS_GET_FILEVAL));
    util_json_object_add_boolean (request, ".flag_dirval",
                                  (flags & KVS_GET_DIRVAL));
    json_object_object_add (request, key, NULL);
    assert (kvs_config.request != NULL);
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
    if (dirp)
        *dirp = kvsdir_alloc (h, key, val, flags);
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

kvsdir_t kvsdir_create (void *h, const char *fmt, ...)
{
    kvsdir_t dir;
    char *key;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        return (NULL);
    va_end (ap);
    if (kvs_get_dir (h, key, &dir, 0) < 0)
        dir = NULL;
    free (key);
    return (dir);
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
    if (valp)
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
    if (valp)
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
    if (valp)
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
    if (valp)
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
    if (valp)
        *valp = json_object_get_boolean (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

bool kvsdir_exists (kvsdir_t dir, const char *name)
{
    return (kvsdir_get (dir, name, NULL) == 0 || errno == EISDIR);
}

bool kvsdir_isdir (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_dir (dir, name, NULL) == 0);
}

bool kvsdir_isstring (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_string (dir, name, NULL) == 0);
}

bool kvsdir_isint (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_int (dir, name, NULL) == 0);
}

bool kvsdir_isint64 (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_int64 (dir, name, NULL) == 0);
}

bool kvsdir_isdouble (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_double (dir, name, NULL) == 0);
}

bool kvsdir_isboolean (kvsdir_t dir, const char *name)
{
    return (kvsdir_get_boolean (dir, name, NULL) == 0);
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

static int dirent_get (kvsdir_t dir, const char *name, json_object **valp)
{
    json_object *dirent, *val;
    int rc = -1;

    if (!(dirent = json_object_object_get (dir->o, name))) {
        errno = ENOENT;
        goto done;
    }
    if (json_object_object_get (dirent, "DIRVAL")
     || json_object_object_get (dirent, "DIRREF")) {
        errno = EISDIR;
        goto done;
    }
    if (!(dir->flags & KVS_GET_FILEVAL)
                || !(val = json_object_object_get (dirent, "FILEVAL"))) {
        errno = ESRCH;
        goto done;
    }
    if (valp) {
        json_object_get (val);
        *valp = val;
    }
    rc = 0;
done:
    return rc;
}

static int dirent_get_dir (kvsdir_t dir, const char *name, kvsdir_t *dirp)
{
    json_object *dirent, *val;
    int rc = -1;

    if (!(dirent = json_object_object_get (dir->o, name))) {
        errno = ENOENT;
        goto done;
    }
    if (json_object_object_get (dirent, "FILEVAL")
     || json_object_object_get (dirent, "FILEREF")) {
        errno = ENOTDIR;
        goto done;
    }
    if (!(dir->flags & KVS_GET_DIRVAL)
                || !(val = json_object_object_get (dirent, "DIRVAL"))) {
        errno = ESRCH; 
        goto done;
    }
    if (dirp)
        *dirp = kvsdir_alloc (dir->handle, name, val, dir->flags);
    rc = 0;
done:
    return rc;
}

int kvsdir_get (kvsdir_t dir, const char *name, json_object **valp)
{
    int rc;
    char *key;

    rc = dirent_get (dir, name, valp);
    if (rc < 0 && (errno == ESRCH || strchr (name, '.'))) {
        key = kvsdir_key_at (dir, name);
        rc = kvs_get (dir->handle, key, valp);
        free (key);
    }
    return rc;
}

int kvsdir_get_dir (kvsdir_t dir, const char *name, kvsdir_t *ndir)
{
    int rc;
    char *key;

    rc = dirent_get_dir (dir, name, ndir);
    if (rc < 0 && (errno == ESRCH || strchr (name, '.'))) {
        key = kvsdir_key_at (dir, name);
        rc = kvs_get_dir (dir->handle, key, ndir, 0);
        free (key);
    }
    return rc;
}

int kvsdir_get_string (kvsdir_t dir, const char *name, char **valp)
{
    json_object *o;
    const char *s;
    int rc = -1;

    if (kvsdir_get (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    if (valp) {
        s = json_object_get_string (o);
        *valp = xstrdup (s);
    }
    rc = 0;
done:
    return rc;
}

int kvsdir_get_int (kvsdir_t dir, const char *name, int *valp)
{
    json_object *o;
    int rc = -1;

    if (kvsdir_get (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int (o);
    rc = 0;
done:
    return rc;
}

int kvsdir_get_int64 (kvsdir_t dir, const char *name, int64_t *valp)
{
    json_object *o;
    int rc = -1;

    if (kvsdir_get (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int64 (o);
    rc = 0;
done:
    return rc;
}

int kvsdir_get_double (kvsdir_t dir, const char *name, double *valp)
{
    json_object *o;
    int rc = -1;

    if (kvsdir_get (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_double) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_double (o);
    rc = 0;
done:
    return rc;
}

int kvsdir_get_boolean (kvsdir_t dir, const char *name, bool *valp)
{
    json_object *o;
    int rc = -1;

    if (kvsdir_get (dir, name, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_boolean) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
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
    assert (kvs_config.request != NULL);
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


int kvsdir_put (kvsdir_t dir, const char *name, json_object *val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put (dir->handle, key, val);
        free (key);
    }
    return (rc);
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

int kvsdir_put_string (kvsdir_t dir, const char *name, const char *val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put_string (dir->handle, key, val);
        free (key);
    }
    return (rc);
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

int kvsdir_put_int (kvsdir_t dir, const char *name, int val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put_int (dir->handle, key, val);
        free (key);
    }
    return (rc);
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

int kvsdir_put_int64 (kvsdir_t dir, const char *name, int64_t val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put_int64 (dir->handle, key, val);
        free (key);
    }
    return (rc);
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

int kvsdir_put_double (kvsdir_t dir, const char *name, double val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put_double (dir->handle, key, val);
        free (key);
    }
    return (rc);
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

int kvsdir_put_boolean (kvsdir_t dir, const char *name, bool val)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_put_boolean (dir->handle, key, val);
        free (key);
    }
    return (rc);
}


int kvs_unlink (void *h, const char *key)
{
    return kvs_put (h, key, NULL);
}

int kvsdir_unlink (kvsdir_t dir, const char *name)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_unlink (dir->handle, key);
        free (key);
    }
    return (rc);
}

int kvs_mkdir (void *h, const char *key)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum = 0;
    int ret = -1;
  
    util_json_object_add_boolean (request, ".flag_mkdir", true);
    json_object_object_add (request, key, NULL); 
    assert (kvs_config.request != NULL);
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

int kvsdir_mkdir (kvsdir_t dir, const char *name)
{
    int rc = -1;
    char *key;
    if ((key = kvsdir_key_at (dir, name))) {
        rc = kvs_mkdir (dir->handle, key);
        free (key);
    }
    return (rc);
}

/* helper for cmb_kvs_commit, cmb_kvs_fence */
static int send_kvs_flush (void *h)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int errnum = 0;
    int ret = -1;
   
    assert (kvs_config.request != NULL);
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
    assert (kvs_config.request != NULL);
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
    if (!kvs_config.barrier) {
        errno = EINVAL;
        return -1;
    }
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
 
    assert (kvs_config.request != NULL);
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

void kvs_reqfun_set (KVSReqF *fun)
{
    kvs_config.request = fun;
}

void kvs_barrierfun_set (KVSBarrierF *fun)
{
    kvs_config.barrier = fun;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
