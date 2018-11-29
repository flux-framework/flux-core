#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs_util_private.h"

void kvs_util_normalize_key_path_tests (void)
{
    char *s;
    bool dirflag;

    s = kvs_util_normalize_key ("a.b.c..d.e", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == false,
        "kvs_util_normalize_key transforms consecutive path separators to one");
    free (s);

    s = kvs_util_normalize_key (".a.b.c.d.e", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == false,
        "kvs_util_normalize_key drops one leading path separator");
    free (s);

    s = kvs_util_normalize_key ("....a.b.c.d.e", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == false,
        "kvs_util_normalize_key drops several leading path separators");
    free (s);

    s = kvs_util_normalize_key ("a.b.c.d.e.", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == true,
        "kvs_util_normalize_key drops one trailing path separator");
    free (s);

    s = kvs_util_normalize_key ("a.b.c.d.e.....", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == true,
        "kvs_util_normalize_key drops several trailing path separators");
    free (s);

    s = kvs_util_normalize_key (".a....b.c.....d..e.....", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == true,
        "kvs_util_normalize_key fixes a big mess");
    free (s);

    s = kvs_util_normalize_key (".", &dirflag);
    ok (s != NULL && !strcmp (s, "."),
        "kvs_util_normalize_key leaves one standalone separator as is");
    free (s);

    s = kvs_util_normalize_key ("....", &dirflag);
    ok (s != NULL && !strcmp (s, "."),
        "kvs_util_normalize_key transforms several standalone separators to one");
    free (s);
}

void kvs_namespace_prefix_tests (void)
{
    char *namespace_prefix, *key_suffix;

    ok (kvs_namespace_prefix (NULL, NULL, NULL) == 0,
        "kvs_namespace_prefix returns 0 on all NULL inputs");

    ok (kvs_namespace_prefix ("foo", NULL, NULL) == 0,
        "kvs_namespace_prefix returns 0 on just a keyname");

    ok (kvs_namespace_prefix ("foo/bar", NULL, NULL) == 0,
        "kvs_namespace_prefix returns 0 on key with just a slash");

    ok (kvs_namespace_prefix ("ns:foo", NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_namespace_prefix returns -1 on key with just a ns: and no slash");

    ok (kvs_namespace_prefix ("ns:foo/", NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_namespace_prefix returns -1 on key with only a namespace");

    ok (kvs_namespace_prefix ("ns:/bar", NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_namespace_prefix returns -1 on key without namespace between ns: and /");

    ok (kvs_namespace_prefix ("ns:foo/ns:bar", NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_namespace_prefix returns -1 on key with chained namespaces");

    ok (kvs_namespace_prefix ("ns:foo/bar", &namespace_prefix, &key_suffix) == 1,
        "kvs_namespace_prefix returns 1 on key with a namespace prefix");

    ok (!strcmp (namespace_prefix, "foo"),
        "kvs_namespace_prefix returned correct namespace");

    ok (!strcmp (key_suffix, "bar"),
        "kvs_key_suffix_parse returned correct key_suffix");

    free (namespace_prefix);
    free (key_suffix);

    ok (kvs_namespace_prefix ("ns:baz/boo", &namespace_prefix, NULL) == 1,
        "kvs_namespace_prefix returns 1, pass in only prefix pointer");

    ok (!strcmp (namespace_prefix, "baz"),
        "kvs_namespace_prefix returned correct namespace");

    free (namespace_prefix);

    ok (kvs_namespace_prefix ("ns:baz/boo", NULL, &key_suffix) == 1,
        "kvs_namespace_prefix returns 1, pass in only prefix pointer");

    ok (!strcmp (key_suffix, "boo"),
        "kvs_key_suffix_parse returned correct key_suffix");

    free (key_suffix);

    ok (kvs_namespace_prefix ("ns:foo/.", &namespace_prefix, &key_suffix) == 1,
        "kvs_namespace_prefix returns 1 on key with a namespace prefix and . suffix");

    ok (!strcmp (namespace_prefix, "foo"),
        "kvs_namespace_prefix returned correct namespace");

    ok (!strcmp (key_suffix, "."),
        "kvs_key_suffix_parse returned correct key_suffix");

    free (namespace_prefix);
    free (key_suffix);
}


int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    kvs_util_normalize_key_path_tests ();
    kvs_namespace_prefix_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
