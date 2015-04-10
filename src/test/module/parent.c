#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <dlfcn.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    char *name;
    int size;
    char *digest;
    int idle;
    void *dso;
    mod_main_f *main;
} module_t;

static zhash_t *modules = NULL;

/* Calculate file digest using zfile() class from czmq.
 * Caller must free.
 */
char *digest (const char *path)
{
    zfile_t *zf = zfile_new (NULL, path);
    char *digest = NULL;
    if (zf)
        digest = xstrdup (zfile_digest (zf));
    zfile_destroy (&zf);
    return digest;
}

static void module_destroy (module_t *m)
{
    if (m->name)
        free (m->name);
    if (m->digest)
        free (m->digest);
    if (m->dso)
        dlclose (m->dso);
    free (m);
}

static module_t *module_create (const char *path, char *argz, size_t argz_len)
{
    module_t *m = xzmalloc (sizeof (*m));
    struct stat sb;
    char **av = NULL;

    if (stat (path, &sb) < 0 || !(m->name = flux_modname (path))
                             || !(m->digest = digest (path))) {
        module_destroy (m);
        errno = ESRCH;
        return NULL;
    }
    m->size = sb.st_size;
    m->dso = dlopen (path, RTLD_NOW | RTLD_LOCAL);
    if (!m->dso || !(m->main = dlsym (m->dso, "mod_main"))) {
        module_destroy (m);
        errno = EINVAL;
        return NULL;
    }
    av = xzmalloc (sizeof (av[0]) * (argz_count (argz, argz_len) + 1));
    argz_extract (argz, argz_len, av);
    if (m->main (NULL, argz_count (argz, argz_len), av) < 0) {
        module_destroy (m);
        errno = EINVAL;
        return NULL;
    }
    if (zhash_lookup (modules, m->name)) {
        module_destroy (m);
        errno = EEXIST;
        return NULL;
    }
    zhash_update (modules, m->name, m);
    zhash_freefn (modules, m->name, (zhash_free_fn *)module_destroy);
    if (av)
        free (av);
    return m;
}

static JSON module_list (void)
{
    JSON o = flux_lsmod_json_create ();
    zlist_t *keys = zhash_keys (modules);
    module_t *m;
    char *name;
    int rc;

    if (!keys)
        oom ();
    name = zlist_first (keys);
    while (name) {
        m = zhash_lookup (modules, name);
        assert (m != NULL);
        rc = flux_lsmod_json_append (o, m->name, m->size, m->digest, m->idle);
        assert (rc == 0);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return o;
}

static int insmod_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    char *path = NULL;
    char *argz = NULL;
    size_t argz_len = 0;
    module_t *m = NULL;
    int errnum;

    if (flux_insmod_request_decode (*zmsg, &path, &argz, &argz_len) < 0)
        errnum = errno;
    else if (!(m = module_create (path, argz, argz_len)))
        errnum = errno;
    else {
        flux_log (h, LOG_DEBUG, "insmod %s", m->name);
        errnum = 0;
    }
    if (flux_err_respond (h, errnum, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
    }
    if (path)
        free (path);
    if (argz)
        free (argz);
    zmsg_destroy (zmsg);
    return 0;
}

static int rmmod_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    char *name = NULL;
    int errnum;

    if (flux_rmmod_request_decode (*zmsg, &name) < 0)
        errnum = errno;
    else if (!zhash_lookup (modules, name))
        errnum = ENOENT;
    else {
        zhash_delete (modules, name);
        flux_log (h, LOG_DEBUG, "rmmod %s", name);
        errnum = 0;
    }
    if (flux_err_respond (h, errnum, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
    }
    if (name)
        free (name);
    zmsg_destroy (zmsg);
    return 0;
}

static int lsmod_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON out = NULL;
    int errnum;

    if (flux_lsmod_request_decode (*zmsg) < 0)
        errnum = errno;
    else if (!(out = module_list ()))
        errnum = errno;
    else
        errnum = 0;
    if (errnum) {
        if (flux_err_respond (h, errnum, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
    } else {
        if (flux_json_respond (h, out, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                      strerror (errno));
    }
    Jput (out);
    zmsg_destroy (zmsg);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST, "parent.insmod",         insmod_request_cb },
    { FLUX_MSGTYPE_REQUEST, "parent.rmmod",          rmmod_request_cb },
    { FLUX_MSGTYPE_REQUEST, "parent.lsmod",          lsmod_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    if (!(modules = zhash_new ()))
        oom ();
    if (flux_msghandler_addvec (h, htab, htablen, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    zhash_destroy (&modules);
    return 0;
}

MOD_NAME ("parent");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
