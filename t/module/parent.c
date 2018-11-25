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
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

typedef struct {
    char *name;
    int size;
    char *digest;
    int idle;
    int status;
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

static flux_modlist_t *module_list (void)
{
    flux_modlist_t *mods = flux_modlist_create ();
    zlist_t *keys = zhash_keys (modules);
    module_t *m;
    char *name;
    int rc;

    assert (mods != NULL);
    if (!keys)
        oom ();
    name = zlist_first (keys);
    while (name) {
        m = zhash_lookup (modules, name);
        assert (m != NULL);
        rc = flux_modlist_append (mods, m->name, m->size, m->digest, m->status,
                                                                     m->idle);
        assert (rc == 0);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return mods;
}

static void insmod_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    char *path = NULL;
    char *argz = NULL;
    size_t argz_len = 0;
    module_t *m = NULL;
    int rc = -1, saved_errno;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!json_str) {
        saved_errno = EPROTO;
        goto done;
    }
    if (flux_insmod_json_decode (json_str, &path, &argz, &argz_len) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!(m = module_create (path, argz, argz_len))) {
        saved_errno = errno;
        goto done;
    }
    flux_log (h, LOG_DEBUG, "insmod %s", m->name);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    if (path)
        free (path);
    if (argz)
        free (argz);
}

static void rmmod_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!zhash_lookup (modules, name)) {
        errno = ENOENT;
        goto error;
    }
    zhash_delete (modules, name);
    flux_log (h, LOG_DEBUG, "rmmod %s", name);
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void lsmod_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    flux_modlist_t *mods = NULL;
    char *json_str = NULL;
    int rc = -1, saved_errno;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!(mods = module_list ())) {
        saved_errno = errno;
        goto done;
    }
    if (!(json_str = flux_lsmod_json_encode (mods))) {
        saved_errno = errno;
        goto done;
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              rc < 0 ? NULL : json_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    if (json_str)
        free (json_str);
    if (mods)
        flux_modlist_destroy (mods);
}

const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "parent.insmod",         insmod_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "parent.rmmod",          rmmod_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "parent.lsmod",          lsmod_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int saved_errno;
    flux_msg_handler_t **handlers = NULL;

    if (argc == 1 && !strcmp (argv[0], "--init-failure")) {
        flux_log (h, LOG_INFO, "aborting during init per test request");
        saved_errno = EIO;
        goto error;
    }
    if (!(modules = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_msghandler_addvec");
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    zhash_destroy (&modules);
    return 0;
error:
    flux_msg_handler_delvec (handlers);
    zhash_destroy (&modules);
    errno = saved_errno;
    return -1;
}

MOD_NAME ("parent");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
