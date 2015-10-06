#include "src/common/libtap/tap.h"
#include "src/common/libutil/optparse.h"

static void *myfatal_h = NULL;

void myfatal (void *h, int exit_code, const char *fmt, ...)
{
    myfatal_h = h;
}

void test_convenience_accessors (void)
{
    struct optparse_option opts [] = {
{ .name = "foo", .key = 1, .has_arg = 0,                .usage = "" },
{ .name = "bar", .key = 2, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "baz", .key = 3, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "mnf", .key = 4, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "oop", .key = 5, .has_arg = 1, .arginfo = "", .usage = "" },
        OPTPARSE_TABLE_END,
    };

    char *av[] = { "test", "--foo", "--baz=hello", "--mnf=7", NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;
    int rc, optind;

    optparse_t *p = optparse_create ("test");
    ok (p != NULL, "create object");

    rc = optparse_add_option_table (p, opts);
    ok (rc == OPTPARSE_SUCCESS, "register options");

    optind = optparse_parse_args (p, ac, av);
    ok (optind == ac, "parse options, verify optind");

    /* hasopt
     */
    dies_ok ({ optparse_hasopt (p, "no-exist"); },
            "hasopt exits on unknown arg");
    lives_ok ({ optparse_hasopt (p, "foo"); },
            "hasopt lives on known arg");
    ok (optparse_hasopt (p, "foo"), "hasopt finds present option");
    ok (!optparse_hasopt (p, "bar"), "hasopt doesn't find missing option");
    ok (optparse_hasopt (p, "baz"), "hasopt finds option with argument");

    /* get_int
     */
    dies_ok ({optparse_get_int (p, "no-exist", 0); },
            "get_int exits on unknown arg");
    dies_ok ({optparse_get_int (p, "foo", 0); },
            "get_int exits on option with no argument");
    dies_ok ({optparse_get_int (p, "baz", 0); },
            "get_int exits on option with wrong type argument");
    lives_ok ({optparse_get_int (p, "bar", 0); },
            "get_int lives on known arg");
    ok (optparse_get_int (p, "bar", 42) == 42,
            "get_int returns default argument when arg not present");
    ok (optparse_get_int (p, "mnf", 42) == 7,
            "get_int returns arg when present");

    /* get_str
     */
    dies_ok ({optparse_get_str (p, "no-exist", NULL); },
            "get_str exits on unknown arg");
    ok (optparse_get_str (p, "foo", "xyz") == NULL,
            "get_str returns NULL on option with no argument configured");
    lives_ok ({optparse_get_str (p, "bar", NULL); },
            "get_str lives on known arg");
    ok (optparse_get_str (p, "bar", NULL) == NULL,
            "get_str returns default argument when arg not present");
    like (optparse_get_str (p, "baz", NULL), "^hello$",
            "get_str returns arg when present");

    /* fatalerr
     */
    dies_ok ({ optparse_hasopt (p, "no-exist"); },
            "hasopt exits on unknown arg");

    rc = optparse_set (p, OPTPARSE_FATALERR_FN, myfatal);
    ok (rc == OPTPARSE_SUCCESS, "optparse_set FATALERR_FN");
    rc = optparse_set (p, OPTPARSE_FATALERR_HANDLE, stderr);
    ok (rc == OPTPARSE_SUCCESS, "optparse_set FATALERR_HANDLE");
    lives_ok ({optparse_get_int (p, "no-exist", 0); },
            "get_int now survives unknown arg");
    ok (myfatal_h == stderr, "handle successfully passed to fatalerr");
    optparse_destroy (p);
}

int main (int argc, char *argv[])
{

    plan (24);

    test_convenience_accessors (); /* 24 tests */

    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
