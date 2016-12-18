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
    flux_module_t *m;
    int size;
    char *digest;
    int idle;
    int status;
    mod_main_f main;
} module_t;

static zhash_t *modules = NULL;
static flux_extensor_t *extensor = NULL;

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
    if (m->m)
        flux_module_destroy (m->m);
    if (m->digest)
        free (m->digest);
    free (m);
}

static module_t *module_create (const char *path, char *argz, size_t argz_len)
{
    module_t *m = xzmalloc (sizeof (*m));
    struct stat sb;
    char **av = NULL;

    if (stat (path, &sb) < 0
        || !(m->m = flux_module_create (extensor, path, 0))
        || !(m->digest = digest (path))) {
        module_destroy (m);
        errno = ESRCH;
        return NULL;
    }
    m->size = sb.st_size;
    if (flux_module_load (m->m) < 0
        || !(m->main = flux_module_lookup (m->m, "mod_main"))) {
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
    if (zhash_lookup (modules, flux_module_name (m->m))) {
        module_destroy (m);
        errno = EEXIST;
        return NULL;
    }
    zhash_update (modules, flux_module_name (m->m), m);
    zhash_freefn (modules, flux_module_name (m->m),
                  (zhash_free_fn *)module_destroy);
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
        rc = flux_modlist_append (mods, name, m->size, m->digest, m->status,
                                                                  m->idle);
        assert (rc == 0);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return mods;
}

static void insmod_request_cb (flux_t *h, flux_msg_handler_t *w,
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
    if (flux_insmod_json_decode (json_str, &path, &argz, &argz_len) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!(m = module_create (path, argz, argz_len))) {
        saved_errno = errno;
        goto done;
    }
    flux_log (h, LOG_DEBUG, "insmod %s", flux_module_name (m->m));
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    if (path)
        free (path);
    if (argz)
        free (argz);
}

static void rmmod_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    char *name = NULL;
    int rc = -1, saved_errno;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (flux_rmmod_json_decode (json_str, &name) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!zhash_lookup (modules, name)) {
        saved_errno = errno = ENOENT;
        goto done;
    }
    zhash_delete (modules, name);
    flux_log (h, LOG_DEBUG, "rmmod %s", name);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    if (name)
        free (name);
}

static void lsmod_request_cb (flux_t *h, flux_msg_handler_t *w,
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

struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "parent.insmod",         insmod_request_cb },
    { FLUX_MSGTYPE_REQUEST, "parent.rmmod",          rmmod_request_cb },
    { FLUX_MSGTYPE_REQUEST, "parent.lsmod",          lsmod_request_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int saved_errno;

    if (argc == 1 && !strcmp (argv[0], "--init-failure")) {
        flux_log (h, LOG_INFO, "aborting during init per test request");
        saved_errno = EIO;
        goto error;
    }
    if (!(modules = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(extensor = flux_extensor_create ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, NULL) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_msghandler_addvec");
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    flux_extensor_destroy (extensor);
    zhash_destroy (&modules);
    return 0;
error:
    zhash_destroy (&modules);
    errno = saved_errno;
    return -1;
}

MOD_NAME ("parent");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
