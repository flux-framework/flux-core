#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "src/modules/kvs/kvs_util.h"

int main (int argc, char *argv[])
{
    char *s;
    bool dirflag;

    plan (NO_PLAN);

    s = kvs_util_normalize_key ("a.b.c.d.e", &dirflag);
    ok (s != NULL && !strcmp (s, "a.b.c.d.e") && dirflag == false,
        "kvs_util_normalize_key works on normal key");
    free (s);

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

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
