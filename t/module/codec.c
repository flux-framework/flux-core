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

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_helpers (); // 9

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
