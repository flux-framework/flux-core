#include "src/common/libflux/module.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/argv.h"
#include "src/common/libtap/tap.h"

void test_helpers (void)
{
    char *name, *path;

    path = xasprintf ("%s/kvs/.libs/kvs.so", MODULE_PATH);
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
    ok (!flux_modfind (MODULE_PATH, "foo"),
        "flux_modfind fails to find unknown module in moduledir");

    path = xasprintf ("%s/kvs/.libs", MODULE_PATH);
    name = flux_modfind (path, "kvs");
    ok ((name != NULL),
        "flux_modfind finds kvs in flat directory");
    if (name)
        free (name);
    free (path);

    name = flux_modfind (MODULE_PATH, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in moduledir");
    if (name)
        free (name);

    path = xasprintf ("foo:bar:xyz:%s:zzz", MODULE_PATH);
    name = flux_modfind (path, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in search path");
    if (name)
        free (name);
    free (path);
}

void test_lsmod_codec (void)
{
    JSON o;
    int len;
    int idle, size;
    const char *name, *digest;

    o = flux_lsmod_json_create ();
    ok (o != NULL,
        "flux_lsmod_json_create works");
    ok (flux_lsmod_json_append (o, "foo", 42, "aa", 3) == 0,
        "first flux_lsmod_json_append works");
    ok (flux_lsmod_json_append (o, "bar", 43, "bb", 2) == 0,
        "second flux_lsmod_json_append works");
    ok (flux_lsmod_json_decode (o, &len) == 0 && len == 2,
        "flux_lsmod_json_decode works");
    ok (flux_lsmod_json_decode_nth (o, 0, &name, &size, &digest, &idle) == 0
        && name && size == 42 && digest && idle == 3
        && !strcmp (name, "foo") && !strcmp (digest, "aa"),
        "flux_lsmod_json_decode_nth(0) works");
    ok (flux_lsmod_json_decode_nth (o, 1, &name, &size, &digest, &idle) == 0
        && name && size == 43 && digest && idle == 2
        && !strcmp (name, "bar") && !strcmp (digest, "bb"),
        "flux_lsmod_json_decode_nth(1) works");

    Jput (o);
}

void test_rmmod_codec (void)
{
    JSON o;
    char *s = NULL;

    o = flux_rmmod_json_encode ("xyz");
    ok (o != NULL,
        "flux_rmmod_json_encode works");
    ok (flux_rmmod_json_decode (o, &s) == 0 && s != NULL && !strcmp (s, "xyz"),
        "flux_rmmod_json_decode works");
    free (s);
    Jput (o);
}

void test_insmod_codec (void)
{
    int ac, argc = 2;
    char *argv[] = { "foo", "bar" };
    char **av;
    JSON o;
    char *s;

    o = flux_insmod_json_encode ("/foo/bar", argc, argv);
    ok (o != NULL,
        "flux_insmod_json_encode works");
    ok (flux_insmod_json_decode (o, &s, &ac, &av) == 0
        && s != NULL && !strcmp (s, "/foo/bar")
        && ac == 2 && !strcmp (av[0], "foo") && !strcmp (av[1], "bar"),
        "flux_insmod_json_decode works");
    argv_destroy (ac, av);
    free (s);
    Jput (o);
}

int main (int argc, char *argv[])
{
    plan (19);

    test_helpers (); // 9
    test_lsmod_codec (); // 6
    test_rmmod_codec (); // 2
    test_insmod_codec (); // 2

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
