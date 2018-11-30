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
#include <jansson.h>

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
static uint32_t rank;

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

    if (stat (path, &sb) < 0 || !(m->name = flux_modname (path, NULL, NULL))
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

/* N.B. services is hardwired to test1,test2,testN, where N is the local
 * broker rank.  This is a specific setup for the flux-module test.  This
 * base component does not perform message routing to its extension modules.
 */
static json_t *module_list (void)
{
    json_t *mods;
    zlist_t *keys;
    module_t *m;
    char *name;
    char rankstr[16];
    int n;

    if (!(mods = json_array ()))
        oom ();
    if (!(keys = zhash_keys (modules)))
        oom ();
    name = zlist_first (keys);
    n = snprintf (rankstr, sizeof (rankstr), "rank%d", (int)rank);
    assert (n < sizeof (rankstr));
    while (name) {
        json_t *o;
        m = zhash_lookup (modules, name);
        if (!(o = json_pack ("{s:s s:i s:s s:i s:i s:[s,s,s]}",
                             "name", m->name,
                             "size", m->size,
                             "digest", m->digest,
                             "idle", m->idle,
                             "status", m->status,
                             "services", "test1", "test2", rankstr)))
            oom ();
        if (json_array_append_new (mods, o) < 0)
            oom ();
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return mods;
}

static void insmod_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    const char *path;
    json_t *args;
    size_t index;
    json_t *value;
    char *argz = NULL;
    size_t argz_len = 0;
    module_t *m = NULL;
    error_t e;

    if (flux_request_unpack (msg, NULL, "{s:s s:o}", "path", &path,
                                                     "args", &args) < 0)
        goto error;
    if (!json_is_array (args))
        goto proto;
    json_array_foreach (args, index, value) {
        if (!json_is_string (value))
            goto proto;
        if ((e = argz_add (&argz, &argz_len, json_string_value (value)))) {
            errno = e;
            goto error;
        }
    }
    if (!(m = module_create (path, argz, argz_len)))
        goto error;
    flux_log (h, LOG_DEBUG, "insmod %s", m->name);
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (argz);
    return;
proto:
    errno = EPROTO;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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
    json_t *mods = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    mods = module_list ();
    if (flux_respond_pack (h, msg, "{s:O}", "mods", mods) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (mods);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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
        errno = EIO;
        goto error;
    }
    if (!(modules = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_get_rank (h, &rank) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0)
        goto error;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    zhash_destroy (&modules);
    return 0;
error:
    saved_errno = errno;
    flux_msg_handler_delvec (handlers);
    zhash_destroy (&modules);
    errno = saved_errno;
    return -1;
}

MOD_NAME ("parent");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
