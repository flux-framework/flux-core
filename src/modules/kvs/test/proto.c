#include <string.h>

#include "src/common/libutil/shortjson.h"
#include "src/modules/kvs/proto.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

void test_get (void)
{
    JSON o;
    bool dir = false;
    bool link = false;
    const char *key = NULL;
    JSON val = NULL;
    int i;

    o = kp_tget_enc ("foo", false, true);
    ok (o != NULL,
        "kp_tget_enc works");
    diag ("get request: %s", Jtostr (o));
    ok (kp_tget_dec (o, &key, &dir, &link) == 0 && dir == false && link == true,
        "kp_tget_dec works");
    like (key, "^foo$",
        "kp_tget_dec returned encoded key");
    Jput (o);

    val = Jnew ();
    Jadd_int (val, "i", 42);
    o = kp_rget_enc ("foo", val);
    val = NULL; /* val now owned by o */
    ok (o != NULL,
        "kp_rget_enc works");
    diag ("get response: %s", Jtostr (o));
    ok (kp_rget_dec (o, &val) == 0,
        "kp_rget_dec works");
    ok (val && Jget_int (val, "i", &i) && i == 42,
        "kp_rget_dec returned encoded object");
    Jput (o); /* owns val */

    o = kp_rget_enc ("foo", NULL);
    ok (o != NULL,
        "kp_rget_enc works with NULL value");
    errno = 0;
    diag ("get response: %s", Jtostr (o));
    ok (kp_rget_dec (o, &val) < 0 && errno == ENOENT,
        "kp_rget_dec returns error with errno = ENOENT if val is NULL");
    Jput (o);
}

void test_watch (void)
{
    JSON o;
    bool dir = false;
    bool once = false;
    bool first = false;
    bool link = false;
    const char *key = NULL;
    const char *s = NULL;
    JSON val;

    val = Jnew ();
    Jadd_str (val, "s", "blatz");
    o = kp_twatch_enc ("foo", val, false, true, false, true);
    ok (o != NULL,
        "kp_twatch_enc works");
    val = NULL;
    diag ("watch request: %s", Jtostr (o));
    ok (kp_twatch_dec (o, &key, &val, &once, &first, &dir, &link) == 0
        && once == false && first == true && dir == false && link == true,
        "kp_twatch_dec works");
    ok (key && !strcmp (key, "foo"),
        "kp_twatch_dec returned encoded key");
    ok (val && Jget_str (val, "s", &s) && !strcmp (s, "blatz"),
        "kp_twatch_dec returned encoded value");
    /* FIXME try encoding NULL value */
    Jput (o);

    val = Jnew ();
    Jadd_str (val, "str", "snerg");
    o = kp_rwatch_enc ("foo", val);
    ok (o != NULL,
        "kp_rwatch_enc works");
    val = NULL;
    diag ("watch response: %s", Jtostr (o));
    ok (kp_rwatch_dec (o, &val) == 0,
        "kp_rewatch_dec works");
    ok (val && Jget_str (val, "str", &s) && !strcmp (s, "snerg"),
        "kp_twatch_dec returned encoded value");
    Jput (o);
}

void test_unwatch (void)
{
    JSON o;
    const char *key;

    o = kp_tunwatch_enc ("foo");
    ok (o != NULL,
        "kp_tunwatch_enc works");
    diag ("unwatch: %s", Jtostr (o));
    ok (kp_tunwatch_dec (o, &key) == 0 && !strcmp (key, "foo"),
        "kp_tunwatch_dec works and returns encoded key");
    Jput (o);
}

void test_fence (void)
{
    JSON o, out;
    JSON ops = Jnew_ar();
    int nprocs;
    const char *name;

    ok ((o = kp_tfence_enc ("foo", 42, ops)) != NULL,
        "kp_tfence_enc works");
    name = NULL;
    nprocs = 0;
    out = NULL;
    diag ("fence: %s", Jtostr (o));
    ok (kp_tfence_dec (o, &name, &nprocs, &out) == 0
        && name != NULL && !strcmp (name, "foo")
        && nprocs == 42 && out != NULL,
        "kp_tfence_dec works");
    Jput (out);
    Jput (o);

    Jput (ops);
}

void test_getroot (void)
{
    JSON o;
    const char *rootdir;
    int rootseq;

    ok ((o = kp_rgetroot_enc (42, "blah")) != NULL,
        "kp_rgetroot_enc works");
    diag ("getroot: %s", Jtostr (o));
    diag ("getroot: %s", Jtostr (o));
    ok (kp_rgetroot_dec (o, &rootseq, &rootdir) == 0
        && rootseq == 42 && rootdir != NULL && !strcmp (rootdir, "blah"),
        "kp_rgetroot_dec works");
    Jput (o);
}

void test_setroot (void)
{
    JSON o;
    const char *rootdir, *name;
    int rootseq;
    JSON root, names;

    names = Jnew_ar ();
    Jadd_ar_str (names, "foo");
    ok ((o = kp_tsetroot_enc (42, "abc", NULL, names)) != NULL,
        "kp_tsetroot_enc works");
    Jput (names);

    diag ("setroot: %s", Jtostr (o));

    ok (kp_tsetroot_dec (o, &rootseq, &rootdir, &root, &names) == 0
        && rootseq == 42 && rootdir != NULL && !strcmp (rootdir, "abc")
        && root == NULL && names != NULL && Jget_ar_str (names, 0, &name)
        && !strcmp (name, "foo"),
        "kp_tsetroot_dec works");
    Jput (o);
}

void test_error (void)
{
    JSON o;
    JSON names;
    const char *name[3];
    int errnum;

    names = Jnew_ar ();
    Jadd_ar_str (names, "foo");
    Jadd_ar_str (names, "bar");
    Jadd_ar_str (names, "baz");
    ok ((o = kp_terror_enc (names, 42)) != NULL,
       "kp_terror_enc works");
    Jput (names);

    diag ("error: %s", Jtostr (o));

    ok (kp_terror_dec (o, &names, &errnum) == 0
        && Jget_ar_str (names, 0, &name[0]) && !strcmp (name[0], "foo")
        && Jget_ar_str (names, 1, &name[1]) && !strcmp (name[1], "bar")
        && Jget_ar_str (names, 2, &name[2]) && !strcmp (name[2], "baz")
        && errnum == 42,
        "kp_terror_dec works");
    Jput (o);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_get ();
    test_watch ();
    test_unwatch ();
    test_getroot ();
    test_setroot ();
    test_fence ();
    test_error ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
