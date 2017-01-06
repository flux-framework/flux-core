
#include <errno.h>
#include <string.h>
#include <libgen.h>

#include "extensor.h"
#include "src/common/libtap/tap.h"

/*
 *  Fake module loader implementation:
 */
struct fake_module {
    char *path;
    char *basename;
    char *last_error;
    int loaded;
};

static int faker_init (flux_module_t *p, const char *path, int flags)
{
    struct fake_module *m = malloc (sizeof (*m));
    if (m == NULL)
        return (-1);
    m->path = strdup (path);
    m->basename = basename (m->path);
    m->loaded = 0;
    m->last_error = NULL;
    flux_module_set_loader_ctx (p, m);
    return 0;
}

static int faker_load (flux_module_t *p)
{
    struct fake_module *m = flux_module_get_loader_ctx (p);
    if (m->loaded) {
        m->last_error = strdup ("already loaded");
        return -1;
    }
    m->loaded = 1;
    return (0);
}

static int faker_unload (flux_module_t *p)
{
    struct fake_module *m = flux_module_get_loader_ctx (p);
    m->loaded = 0;
    return 0;
}

static void faker_destroy (flux_module_t *p)
{
    struct fake_module *m = flux_module_set_loader_ctx (p, NULL);
    free (m->path);
    free (m->last_error);
    free (m);
}

static int faker_is_loaded (flux_module_t *p)
{
    struct fake_module *m = flux_module_get_loader_ctx (p);
    return (m->loaded);
}

static void * faker_lookup (flux_module_t *p, const char *sym)
{
    if (strcmp (sym, "is_loaded") == 0)
        return ((void *) faker_is_loaded);
    return (NULL);
}

static const char * faker_get_name (flux_module_t *p)
{
    struct fake_module *m = flux_module_get_loader_ctx (p);
    return (m->basename);
}

static const char * faker_strerror (flux_module_t *p)
{
    struct fake_module *m = flux_module_get_loader_ctx (p);
    return (m->last_error);
}

struct flux_module_loader fake_loader = {
    .name = "faker",
    .init = faker_init,
    .load = faker_load,
    .unload = faker_unload,
    .destroy = faker_destroy,
    .lookup = faker_lookup,
    .get_name = faker_get_name,
    .strerror = faker_strerror,
    .extensions = { "", NULL }
};


int main (int argc, char *argv[])
{
    int rc;
    flux_extensor_t *s;
    flux_module_t *p, *q;
    int (*is_loaded) (flux_module_t *p);

    plan (NO_PLAN);

    s = flux_extensor_create ();
    ok (s != NULL, "flux_extensor_create");
    rc = flux_extensor_register_loader (s, &fake_loader);
    ok (rc == 0, "flux_extensor_register_loader");
    ok (flux_extensor_get_loader (s, "faker") == &fake_loader,
        "flux_extensor_get_loader");
    ok (flux_extensor_get_loader (s, "nonexistent") == NULL,
        "flux_extensor_get_loader fails for nonexistent loader");

    p = flux_module_create (s, "/this/is/a/test", 0);
    if (!p)
        BAIL_OUT ("can't create fake module: %s", strerror (errno));
    ok (p != NULL, "flux_module_create");
    ok (flux_module_load (p) == 0, "flux_module_load");
    is_loaded = flux_module_lookup (p, "is_loaded");
    ok (is_loaded != NULL, "flux_module_lookup");
    ok ((*is_loaded)(p), "is_loaded (p) is true");
    ok (flux_module_unload (p) == 0, "flux_module_unload");
    ok ((*is_loaded)(p) == 0, "is_loaded (p) is now false");
    ok (flux_module_load (p) == 0, "flux_module_load");

    is (flux_module_path (p), "/this/is/a/test", "flux_module_path works");
    is (flux_module_name (p), "test", "flux_module_name works");
    ok (flux_module_uuid (p) != NULL, "flux_module_uuid = %s", flux_module_uuid (p));

    ok (flux_extensor_get_module (s, "test") == p, "flux_extensor_get_module");
    ok (flux_extensor_get_module (s, "nonexistent") == NULL,
        "flux_extensor_get_module return NULL on nonexistent module");

    q = flux_module_create (s, "/this/is/another/test", 0);
    ok (q != NULL, "flux_module_create different module with same name");
    ok (flux_module_load (q) == 0, "flux_module_load second module");
    ok (flux_extensor_get_module (s, "test") == p,
        "flux_extensor_get_module ('test') still retuns first loaded module");
    ok (flux_module_unload (p) == 0, "flux_module_unload (p)");

    ok (flux_extensor_get_module (s, "test") == q,
        "flux_extensor_get_module ('test') now returns 2nd loaded module");

    ok (flux_module_load (p) == 0, "flux_module_load first module again");
    ok (flux_extensor_get_module (s, "test") == q,
        "flux_extensor_get_module ('test') still returns 2nd loaded module");

    flux_module_destroy (q);

    ok (flux_extensor_get_module (s, "test") == p,
        "flux_extensor_get_module ('test') now returns p");

    flux_module_destroy (p);
    ok (flux_extensor_get_module (s, "test") == NULL,
        "flux_extensor_get_module returns NULL after all modules removed");

    flux_extensor_destroy (s);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

