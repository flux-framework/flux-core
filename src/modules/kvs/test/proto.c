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
    ok (kp_rget_dec (o, &val) == 0,
        "kp_rget_dec works");
    ok (val && Jget_int (val, "i", &i) && i == 42,
        "kp_rget_dec returned encoded object");
    Jput (o); /* owns val */

    o = kp_rget_enc ("foo", NULL);
    ok (o != NULL,
        "kp_rget_enc works with NULL value");
    errno = 0;
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
    ok (kp_tunwatch_dec (o, &key) == 0 && !strcmp (key, "foo"),
        "kp_tunwatch_dec works and returns encoded key");
    Jput (o);
}

void test_commit (void)
{
    JSON o;
    const char *sender = NULL;
    JSON ops = NULL;
    JSON op1, op2;
    const char *rootdir;
    int rootseq;
    int len;

    ok ((o = kp_tcommit_enc (NULL, NULL)) != NULL,
        "kp_tcommit_enc (external commit) works");
    ok (kp_tcommit_dec (o, &sender, &ops) == 0
        && sender == NULL && ops == NULL,
        "kp_tcommit_dec (external commit) works");
    Jput (o);

    op1 = Jnew ();
    op2 = Jnew ();
    ops = Jnew_ar ();
    Jadd_ar_obj (ops, op1);
    Jadd_ar_obj (ops, op2);
    Jput (op1);
    Jput (op2);
    ok ((o = kp_tcommit_enc ("sender", ops)) != NULL,
        "kp_tcommit_enc (internal commit) works");
    Jput (ops);
    ok (kp_tcommit_dec (o, &sender, &ops) == 0
        && sender != NULL && !strcmp (sender, "sender"),
        "kp_tcommit_dec (internal commit) works");
    Jput (o);
    ok (Jget_ar_len (ops, &len) && len == 2,
        "kp_tcommit_dec returned encoded correct number of ops");
    Jput (ops);

    ok ((o = kp_rcommit_enc (42, "abc", "def")) != NULL,
        "kp_rcommit_enc works");
    ok (kp_rcommit_dec (o, &rootseq, &rootdir, &sender) == 0
        && rootseq == 42
        && rootdir != NULL && !strcmp (rootdir, "abc")
        && sender != NULL && !strcmp (sender, "def"),
        "kp_rcommid_dec works");
    Jput (o);
}

void test_getroot (void)
{
    JSON o;
    const char *rootdir;
    int rootseq;

    ok ((o = kp_rgetroot_enc (42, "blah")) != NULL,
        "kp_rgetroot_enc works");
    ok (kp_rgetroot_dec (o, &rootseq, &rootdir) == 0
        && rootseq == 42 && rootdir != NULL && !strcmp (rootdir, "blah"),
        "kp_rgetroot_dec works");
    Jput (o);
}

void test_setroot (void)
{
    JSON o;
    const char *rootdir;
    int rootseq;
    JSON root;

    ok ((o = kp_tsetroot_enc (42, "abc", NULL)) != NULL,
        "kp_tsetroot_enc works");
    ok (kp_tsetroot_dec (o, &rootseq, &rootdir, &root) == 0
        && rootseq == 42 && rootdir != NULL && !strcmp (rootdir, "abc")
        && root == NULL,
        "kp_tsetroot_dec works");
    Jput (o);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_get (); // 8
    test_watch (); // 7
    test_unwatch (); // 2
    test_commit (); // 7
    test_getroot (); // 2
    test_setroot (); // 2

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
