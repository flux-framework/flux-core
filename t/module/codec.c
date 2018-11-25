#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

void test_helpers (void)
{
    char *name, *path;
    const char *modpath = flux_conf_get ("module_path", CONF_FLAG_INTREE);

    path = xasprintf ("%s/kvs/.libs/kvs.so", modpath);
    ok (access (path, F_OK) == 0,
        "built kvs module is located");
    name = flux_modname (path);
    ok ((name != NULL),
        "flux_modname on kvs should find a name");
    skip (name == NULL, 1,
        "skip next test because kvs.so name is NULL");
    like (name, "^kvs$",
        "flux_modname says kvs module is named kvs");
    end_skip;
    if (name)
        free (name);
    free (path);

    ok (!flux_modfind ("nowhere", "foo"),
        "flux_modfind fails with nonexistent directory");
    ok (!flux_modfind (".", "foo"),
        "flux_modfind fails in current directory");
    ok (!flux_modfind (modpath, "foo"),
        "flux_modfind fails to find unknown module in moduledir");

    path = xasprintf ("%s/kvs/.libs", modpath);
    name = flux_modfind (path, "kvs");
    ok ((name != NULL),
        "flux_modfind finds kvs in flat directory");
    if (name)
        free (name);
    free (path);

    name = flux_modfind (modpath, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in moduledir");
    if (name)
        free (name);

    path = xasprintf ("foo:bar:xyz:%s:zzz", modpath);
    name = flux_modfind (path, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in search path");
    if (name)
        free (name);
    free (path);
}

void test_lsmod_codec (void)
{
    flux_modlist_t *mods;
    int idle, size;
    const char *name, *digest;
    char *json_str;
    int status;

    mods = flux_modlist_create ();
    ok (mods != NULL,
        "flux_modlist_create works");
    ok (flux_modlist_append (mods, "foo", 42, "aa", 3, 0) == 0,
        "first flux_modlist_append works");
    ok (flux_modlist_append (mods, "bar", 43, "bb", 2, 1) == 0,
        "second flux_modlist_append works");
    ok (flux_modlist_count (mods) == 2,
        "flux_modlist_count works");
    ok (flux_modlist_get (mods, 0, &name, &size, &digest, &idle, &status) == 0
        && name && size == 42 && digest && idle == 3 && status == 0
        && !strcmp (name, "foo") && !strcmp (digest, "aa"),
        "flux_modlist_get(0) works");
    ok (flux_modlist_get (mods, 1, &name, &size, &digest, &idle, &status) == 0
        && name && size == 43 && digest && idle == 2 && status == 1
        && !strcmp (name, "bar") && !strcmp (digest, "bb"),
        "flux_modlist_get(1) works");

    /* again after encode/decode */
    ok ((json_str = flux_lsmod_json_encode (mods)) != NULL,
        "flux_lsmod_json_encode works");
    flux_modlist_destroy (mods);
    ok ((mods = flux_lsmod_json_decode (json_str)) != NULL,
        "flux_lsmod_json_decode works");
    ok (flux_modlist_count (mods) == 2,
        "flux_modlist_count still works");
    ok (flux_modlist_get (mods, 0, &name, &size, &digest, &idle, &status) == 0
        && name && size == 42 && digest && idle == 3 && status == 0
        && !strcmp (name, "foo") && !strcmp (digest, "aa"),
        "flux_modlist_get(0) still works");
    ok (flux_modlist_get (mods, 1, &name, &size, &digest, &idle, &status) == 0
        && name && size == 43 && digest && idle == 2 && status == 1
        && !strcmp (name, "bar") && !strcmp (digest, "bb"),
        "flux_modlist_get(1) still works");

    flux_modlist_destroy (mods);
    free (json_str);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_helpers (); // 9
    test_lsmod_codec (); // 11

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
