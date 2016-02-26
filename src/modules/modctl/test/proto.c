#include "src/common/libutil/shortjson.h"

#include "src/modules/modctl/proto.h"
#include "src/common/libtap/tap.h"

void test_tload (void)
{
    JSON o;
    char *av[] = { "a", "b", "c" };
    const char *path;
    int argc;
    const char **argv;

    o = modctl_tload_enc ("/foo/bar.so", 3, av);
    ok (o != NULL,
        "modctl_tload_enc works");
    ok (modctl_tload_dec (o, &path, &argc, &argv) == 0 && path && argc == 3,
        "modctl_tload_dec works");
    like (path, "^/foo/bar.so$",
        "modctl_tload_dec returned encoded path");
    like (argv[0], "^a$",
        "modctl_tload_dec returned encoded argv[0]");
    like (argv[1], "^b$",
        "modctl_tload_dec returned encoded argv[1]");
    like (argv[2], "^c$",
        "modctl_tload_dec returned encoded argv[2]");
    free (argv);
    Jput (o);
}

void test_rload (void)
{
    JSON o;
    int errnum;

    o = modctl_rload_enc (42);
    ok (o != NULL,
        "modctl_rload_enc works");
    ok (modctl_rload_dec (o, &errnum) == 0,
        "modctl_rload_dec works");
    ok (errnum == 42,
        "modctl_rload_dec returns encoded errnum");
    Jput (o);

}

void test_tunload (void)
{
    JSON o;
    const char *name = NULL;

    o = modctl_tunload_enc ("bar");
    ok (o != NULL,
        "modctl_tunload_enc works");
    ok (modctl_tunload_dec (o, &name) == 0 && name,
        "modctl_tunload_dec works");
    like (name, "^bar$",
        "modctl_tunload_dec returned encoded module name");
    Jput (o);
}

void test_runload (void)
{
    JSON o;
    int errnum;

    o = modctl_runload_enc (42);
    ok (o != NULL,
        "modctl_runload_enc works");
    ok (modctl_runload_dec (o, &errnum) == 0,
        "modctl_runload_dec works");
    ok (errnum == 42,
        "modctl_runload_dec returns encoded errnum");
    Jput (o);

}

void test_tlist (void)
{
    JSON o;
    const char *svc;

    o = modctl_tlist_enc ("foo");
    ok (o != NULL,
        "modctl_tlist_enc works");
    ok (modctl_tlist_dec (o, &svc) == 0 && svc,
        "modctl_tlist_dec works");
    like (svc, "^foo$",
        "modctl_tlist_dec returned encoded service");
    Jput (o);
}

void test_rlist (void)
{
    JSON o;
    const char *name, *digest;
    int len, errnum, size, idle;
    int status;

    o = modctl_rlist_enc ();
    ok (o != NULL,
        "modctl_rlist_enc works");
    ok (modctl_rlist_enc_add (o, "foo", 42, "abba", 6, 1) == 0,
        "modctl_rlist_enc_add works 0th time");
    ok (modctl_rlist_enc_add (o, "bar", 69, "argh", 19, 2) == 0,
        "modctl_rlist_enc_add works 1st time");
    ok (modctl_rlist_enc_errnum (o, 0) == 0,
        "modctl_rlist_enc_errnum works");
    ok (modctl_rlist_dec (o, &errnum, &len) == 0 && errnum == 0 && len == 2,
        "modctl_rlist_dec works");
    ok (modctl_rlist_dec_nth (o, 0, &name, &size, &digest, &idle, &status) == 0
        && name && size == 42 && digest && idle == 6 && status == 1,
        "modctl_rlist_dec_nth(0) works and returns correct scalar vals");
    like (name, "^foo$",
        "modctl_rlist_dec_nth(0) returns encoded name");
    like (digest, "^abba$",
        "modctl_rlist_dec_nth(0) returns encoded digest");
    ok (modctl_rlist_dec_nth (o, 1, &name, &size, &digest, &idle, &status) == 0
        && name && size == 69 && digest && idle == 19 && status == 2,
        "modctl_rlist_dec_nth(1) works and returns correct scalar vals");
    like (name, "^bar$",
        "modctl_rlist_dec_nth(1) returns encoded name");
    like (digest, "^argh$",
        "modctl_rlist_dec_nth(1) returns encoded digest");

    Jput (o);
}

int main (int argc, char *argv[])
{

    plan (29);

    test_tunload (); // 3
    test_runload (); // 3

    test_tload (); // 6
    test_rload (); // 3

    test_tlist (); // 3
    test_rlist (); // 11

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
