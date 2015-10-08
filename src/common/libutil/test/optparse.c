#include "src/common/libtap/tap.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/sds.h"

static void *myfatal_h = NULL;

void myfatal (void *h, int exit_code, const char *fmt, ...)
{
    myfatal_h = h;
}

sds usage_out = NULL;
void output_f (const char *fmt, ...)
{
    va_list ap;
    sds s = usage_out ? usage_out : sdsempty();
    va_start (ap, fmt);
    usage_out = sdscatvprintf (s, fmt, ap);
    va_end (ap);
}

void usage_ok (optparse_t *p, const char *expected, const char *msg)
{
    optparse_print_usage (p);
    ok (usage_out != NULL, "optparse_print_usage");
    is (usage_out, expected, msg);
    sdsfree (usage_out);
    usage_out = NULL;
}

void test_usage_output (void)
{
    optparse_err_t e;
    optparse_t *p = optparse_create ("prog-foo");
    struct optparse_option opt;
    ok (p != NULL, "optparse_create");

    // Ensure we use default term columns:
    unsetenv ("COLUMNS");

    opt = ((struct optparse_option) {
            .name = "test", .key = 't', .has_arg = 0,
            .usage = "Enable a test option."
            });
    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option");
    opt = ((struct optparse_option) {
            .name = "test2", .key = 'T', .has_arg = 1,
            .arginfo = "N",
            .usage = "Enable a test option N."
            });
    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option");

    e = optparse_set (p, OPTPARSE_USAGE, "[OPTIONS]");
    ok (e == OPTPARSE_SUCCESS, "optparse_set (USAGE)");

    e = optparse_set (p, OPTPARSE_LOG_FN, output_f);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (LOG_FN)");

    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n\
  -t, --test             Enable a test option.\n",
        "Usage output as expected");

    e = optparse_set (p, OPTPARSE_LEFT_MARGIN, 0);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (LEFT_MARGIN)");

    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
-T, --test2=N            Enable a test option N.\n\
-h, --help               Display this message.\n\
-t, --test               Enable a test option.\n",
        "Usage output as expected w/ left margin");

    e = optparse_set (p, OPTPARSE_LEFT_MARGIN, 2);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (LEFT_MARGIN)");

    // Remove options
    e = optparse_remove_option (p, "test");
    ok (e == OPTPARSE_SUCCESS, "optparse_remove_option (\"test\")");

    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n",
        "Usage output as expected after option removal");

    // Add doc sections
    e = optparse_add_doc (p, "This is some doc in header", 0);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_doc (group=0)");
    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n",
        "Usage output as with doc");

    // Add a longer option in group 1:
    opt = ((struct optparse_option) {
            .name = "long-option", .key = 'A', .has_arg = 1, .group = 1,
            .arginfo = "ARGINFO",
            .usage = "Enable a long option with argument info ARGINFO."
            });
    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option. group 1.");

    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n\
  -A, --long-option=ARGINFO\n\
                         Enable a long option with argument info ARGINFO.\n",
        "Usage output with option in group 1");

    // Add doc for group 1.
    e = optparse_add_doc (p, "This is some doc for group 1", 1);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_doc (group = 1)");
    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n\
This is some doc for group 1\n\
  -A, --long-option=ARGINFO\n\
                         Enable a long option with argument info ARGINFO.\n",
        "Usage output with option in group 1");


    // Increase option width:
    e = optparse_set (p, OPTPARSE_OPTION_WIDTH, 30);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (OPTION_WIDTH)");
    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N               Enable a test option N.\n\
  -h, --help                  Display this message.\n\
This is some doc for group 1\n\
  -A, --long-option=ARGINFO   Enable a long option with argument info ARGINFO.\n",
        "Usage output with increased option width");

    // Add an option with very long description in group 1:
    opt = ((struct optparse_option) {
            .name = "option-B", .key = 'B', .group = 1,
            .usage = "This option has a very long description. It should be split across lines nicely."
            });
    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option. group 1.");

    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N               Enable a test option N.\n\
  -h, --help                  Display this message.\n\
This is some doc for group 1\n\
  -A, --long-option=ARGINFO   Enable a long option with argument info ARGINFO.\n\
  -B, --option-B              This option has a very long description. It should\n\
                              be split across lines nicely.\n",
        "Usage output with message autosplit across lines");

    optparse_destroy (p);
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

void test_errors (void)
{
    optparse_err_t e;
    struct optparse_option opt;
    optparse_t *p = optparse_create ("errors-test");
    ok (p != NULL, "optparse_create");

    opt = ((struct optparse_option) {
            .name = "help", .key = 'h',
            .usage = "Conflicting option"
            });

    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_EEXIST, "optparse_add_option: Errror with EEXIST");
    e = optparse_add_option (NULL, &opt);
    ok (e == OPTPARSE_BAD_ARG, "optparse_add_option: BAD_ARG with invalid optparse_t");

    e = optparse_remove_option (p, "foo");
    ok (e == OPTPARSE_FAILURE, "optparse_remove_option: FAILURE if option not found");

    // optparse_set error cases:
    e = optparse_set (p, 1000, 1);
    ok (e == OPTPARSE_BAD_ARG, "optparse_set (invalid item) returns BAD_ARG");

    e = optparse_set (p, OPTPARSE_LEFT_MARGIN, 2000);
    ok (e == OPTPARSE_BAD_ARG, "optparse_set (LEFT_MARGIN, 2000) returns BAD_ARG");
    e = optparse_set (p, OPTPARSE_LEFT_MARGIN, -1);
    ok (e == OPTPARSE_BAD_ARG, "optparse_set (LEFT_MARGIN, -1) returns BAD_ARG");

    e = optparse_set (p, OPTPARSE_OPTION_WIDTH, 2000);
    ok (e == OPTPARSE_BAD_ARG, "optparse_set (OPTION_WIDTH, 2000) returns BAD_ARG");
    e = optparse_set (p, OPTPARSE_OPTION_WIDTH, -1);
    ok (e == OPTPARSE_BAD_ARG, "optparse_set (OPTION_WIDTH, -1) returns BAD_ARG");


    optparse_destroy (p);
}

void test_multiret (void)
{
    int rc;
    const char *optarg;
    optparse_err_t e;
    optparse_t *p = optparse_create ("multret-test");
    struct optparse_option opts [] = {
    { .name = "required-arg", .key = 'r', .has_arg = 1,
      .arginfo = "", .usage = "" },
    { .name = "optional-arg", .key = 'o', .has_arg = 2,
      .arginfo = "", .usage = "" },
    { .name = "multi-ret",    .key = 'm', .has_arg = 3,
      .arginfo = "", .usage = "" },
      OPTPARSE_TABLE_END,
    };

    char *av[] = { "multret-test",
                   "-r", "one", "-mone", "-m", "two",
                   "-o", "-rtwo", "--multi-ret=a,b,c",
                   NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;

    ok (p != NULL, "optparse_create");

    e = optparse_add_option_table (p, opts);
    ok (e == OPTPARSE_SUCCESS, "register options");

    optind = optparse_parse_args (p, ac, av);
    ok (optind == ac, "parse options, verify optind");

    rc = optparse_getopt (p, "required-arg", &optarg);
    ok (rc == 2, "-r used twice");
    is (optarg, "two", "last usage wins");

    optarg = NULL;
    rc = optparse_getopt (p, "optional-arg", &optarg);
    ok (rc == 1, "-o used once");
    ok (optarg == NULL, "with no arg");

    optarg = NULL;
    rc = optparse_getopt (p, "multi-ret", &optarg);
    ok (rc == 3, "-m used three times");
    is (optarg, "c", "last usage wins");

    // iterate over arguments
    int i = 0;
    char *expected[] = { "one", "two", "BAD INDEX" };
    const char *s;
    while ((s = optparse_getopt_next (p, "required-arg"))) {
        is (expected [i], s, "%d: argument matches", i);
        i++;
    }
    ok (optparse_getopt_next (p, "required-arg")  == NULL,
        "getopt_next returns Null repeatedly after iteration");

    rc = optparse_getopt_iterator_reset (p, "required-arg");
    ok (rc == 2, "Iterator reset indicates 2 options to iterate");

    // multi-ret
    char *expected2[] = { "one", "two", "a", "b", "c" };
    i = 0;
    while ((s = optparse_getopt_next (p, "multi-ret"))) {
        is (expected2 [i], s, "%d: argument matches", i);
        i++;
    }
    rc = optparse_getopt_iterator_reset (p, "multi-ret");
    ok (rc == 5, "Iterator reset indicates 2 options to iterate");

    optparse_destroy (p);
}

int main (int argc, char *argv[])
{

    plan (81);

    test_convenience_accessors (); /* 24 tests */
    test_usage_output (); /* 29 tests */
    test_errors (); /* 9 tests */
    test_multiret (); /* 19 tests */

    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
