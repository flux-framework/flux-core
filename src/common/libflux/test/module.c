#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

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

void test_rmmod_codec (void)
{
    char *json_str;
    char *s = NULL;

    json_str = flux_rmmod_json_encode ("xyz");
    ok (json_str != NULL,
        "flux_rmmod_json_encode works");
    ok (flux_rmmod_json_decode (json_str, &s) == 0
        && s != NULL && !strcmp (s, "xyz"),
        "flux_rmmod_json_decode works");
    free (s);
    free (json_str);
}

void test_insmod_codec (void)
{
    int argc = 2;
    char *argv[] = { "foo", "bar", NULL };
    char *argz = NULL;
    size_t argz_len = 0;
    char *av[16];
    char *json_str;
    char *s;
    int rc;

    json_str = flux_insmod_json_encode ("/foo/bar", argc, argv);
    ok (json_str != NULL,
        "flux_insmod_json_encode works");

    rc = flux_insmod_json_decode (json_str, &s, &argz, &argz_len);
    argz_extract (argz, argz_len, av);
    ok (rc == 0 && s != NULL && !strcmp (s, "/foo/bar")
        && !strcmp (av[0], "foo") && !strcmp (av[1], "bar") && av[2] == NULL,
        "flux_insmod_json_decode works");

    if (argz)
        free (argz);
    free (s);
    free (json_str);
}

int main (int argc, char *argv[])
{
    plan (15);

    test_lsmod_codec (); // 11
    test_rmmod_codec (); // 2
    test_insmod_codec (); // 2

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
