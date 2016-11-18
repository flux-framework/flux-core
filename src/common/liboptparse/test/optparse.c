#include "src/common/libtap/tap.h"
#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/sds.h"

static void *myfatal_h = NULL;

int myfatal (void *h, int exit_code)
{
    myfatal_h = h;
    return (0);
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

void usage_output_is (const char *expected, const char *msg)
{
    ok (usage_out != NULL, "optparse_print_usage");
    is (usage_out, expected, msg);
    sdsfree (usage_out);
    usage_out = NULL;
}

void usage_ok (optparse_t *p, const char *expected, const char *msg)
{
    optparse_print_usage (p);
    usage_output_is (expected, msg);
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

    e = optparse_set (p, OPTPARSE_USAGE, "[MOAR OPTIONS]");
    ok (e == OPTPARSE_SUCCESS, "optparse_set (USAGE)");

    // Reset usage works:
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

    // Add a hidden (undocumented) option
    opt = ((struct optparse_option) {
            .name = "hidden", .key = 'H', .has_arg = 1,
            .flags = OPTPARSE_OPT_HIDDEN,
            .arginfo = "ARGINFO",
            .usage = "This option should not be displayed"
            });
    e = optparse_add_option (p, &opt);
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option. group 1.");
    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
  -T, --test2=N          Enable a test option N.\n\
  -h, --help             Display this message.\n\
  -t, --test             Enable a test option.\n",
        "Usage output as expected");


    // Adjust left margin
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

    // Add an option whose description will break up a word
    opt = ((struct optparse_option) {
            .name = "option-C", .key = 'C', .group = 1,
            .usage = "ThisOptionHasAVeryLongWordInTheDescriptionThatShouldBeBrokenAcrossLines."
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
                              be split across lines nicely.\n\
  -C, --option-C              ThisOptionHasAVeryLongWordInTheDescriptionThatSho-\n\
                              uldBeBrokenAcrossLines.\n",
        "Usage output with message autosplit across lines");

    ok (setenv ("COLUMNS", "120", 1) >= 0, "Set COLUMNS=120");
    usage_ok (p, "\
Usage: prog-foo [OPTIONS]\n\
This is some doc in header\n\
  -T, --test2=N               Enable a test option N.\n\
  -h, --help                  Display this message.\n\
This is some doc for group 1\n\
  -A, --long-option=ARGINFO   Enable a long option with argument info ARGINFO.\n\
  -B, --option-B              This option has a very long description. It should be split across lines nicely.\n\
  -C, --option-C              ThisOptionHasAVeryLongWordInTheDescriptionThatShouldBeBrokenAcrossLines.\n",
        "Usage output with COLUMNS=120 not split across lines");

    /* Unset COLUMNS again */
    unsetenv ("COLUMNS");
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
{ .name = "neg", .key = 6, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "dub", .key = 7, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "ndb", .key = 8, .has_arg = 1, .arginfo = "", .usage = "" },
        OPTPARSE_TABLE_END,
    };

    char *av[] = { "test", "--foo", "--baz=hello", "--mnf=7", "--neg=-4",
                   "--dub=5.7", "--ndb=-3.2", NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;
    int rc, optind;

    optparse_t *p = optparse_create ("test");
    ok (p != NULL, "create object");

    rc = optparse_add_option_table (p, opts);
    ok (rc == OPTPARSE_SUCCESS, "register options");

    ok (optparse_optind (p) == -1, "optparse_optind returns -1 before parse");
    optind = optparse_parse_args (p, ac, av);
    ok (optind == ac, "parse options, verify optind");

    ok (optparse_optind (p) == optind, "optparse_optind works after parse");

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
            "get_int exits on option with wrong type argument (string)");
    dies_ok ({optparse_get_int (p, "dub", 0); },
            "get_int exits on option with wrong type argument (float)");
    lives_ok ({optparse_get_int (p, "bar", 0); },
            "get_int lives on known arg");
    ok (optparse_get_int (p, "bar", 42) == 42,
            "get_int returns default argument when arg not present");
    ok (optparse_get_int (p, "mnf", 42) == 7,
            "get_int returns arg when present");
    ok (optparse_get_int (p, "neg", 42) == -4,
            "get_int returns negative arg when present");

    /* get_double
     */
    dies_ok ({optparse_get_double (p, "no-exist", 0); },
            "get_double exits on unknown arg");
    dies_ok ({optparse_get_double (p, "foo", 0); },
            "get_double exits on option with no argument");
    dies_ok ({optparse_get_double (p, "baz", 0); },
            "get_int exits on option with wrong type argument (string)");
    lives_ok ({optparse_get_double (p, "bar", 0); },
            "get_double lives on known arg");
    ok (optparse_get_double (p, "bar", 42.0) == 42.0,
            "get_double returns default argument when arg not present");
    ok (optparse_get_double (p, "mnf", 42) == 7.0,
            "get_double returns arg when present");
    ok (optparse_get_double (p, "neg", 42) == -4.0,
            "get_double returns negative arg when present");
    ok (optparse_get_double (p, "dub", 42) == 5.7,
            "get_double returns arg when present");
    ok (optparse_get_double (p, "ndb", 42) == -3.2,
            "get_double returns negative arg when present");

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
    { .name = "multi-ret",    .key = 'm', .has_arg = 1,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
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

void test_long_only (void)
{
    int rc;
    const char *optarg;
    optparse_err_t e;
    optparse_t *p = optparse_create ("long-only-test");
    struct optparse_option opts [] = {
    { .name = "basic", .key = 'b', .has_arg = 1,
      .arginfo = "B", .usage = "This is a basic argument" },
    { .name = "long-only", .has_arg = 1,
      .arginfo = "L", .usage = "This is a long-only option" },
    { .name = "again-long-only", .has_arg = 0,
      .usage = "Another long-only" },
      OPTPARSE_TABLE_END,
    };

    char *av[] = { "long-only-test",
                   "-b", "one", "--again-long-only",
                   NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;

    ok (p != NULL, "optparse_create");

    e = optparse_add_option_table (p, opts);
    ok (e == OPTPARSE_SUCCESS, "register options");

    optind = optparse_parse_args (p, ac, av);
    ok (optind == ac, "parse options, verify optind");

    rc = optparse_getopt (p, "basic", &optarg);
    ok (rc == 1, "got -b");
    is (optarg, "one", "got correct argument to --basic option");

    optarg = NULL;
    // Ensure we got correct long-only option
    ok (optparse_hasopt (p, "again-long-only"), "Got --again-long-only");
    ok (!optparse_hasopt (p, "long-only"), "And didn't get --long-only");

    char *av2[] = { "long-only-test", "--again-long-only", "-bxxx",
                    "--long-only=foo", NULL };
    ac = sizeof (av2) / sizeof(av2[0]) - 1;

    optind = optparse_parse_args (p, ac, av2);
    ok (optind == ac, "parse options, verify optind");

    optarg = NULL;
    rc = optparse_getopt (p, "basic", &optarg);
    ok (rc == 2, "got -b", rc); // second use of --basic
    is (optarg, "xxx", "got correct argument to --basic option");

    // Ensure we got correct long-only option
    ok (optparse_hasopt (p, "again-long-only"), "Got --again-long-only");
    rc = optparse_getopt (p, "long-only", &optarg);
    ok (rc == 1, "got --long-only");
    is (optarg, "foo", "got correct argument to --long-only option");

    optparse_destroy (p);
}

void test_optional_argument (void)
{
    int rc;
    const char *optarg;
    optparse_err_t e;
    optparse_t *p = optparse_create ("optarg");
    struct optparse_option opts [] = {
    { .name = "basic", .key = 'b', .has_arg = 1,
      .arginfo = "B", .usage = "This is a basic argument" },
    { .name = "optional-arg", .has_arg = 2, .key = 'o',
      .arginfo = "OPTIONAL", .usage = "This has an optional argument" },
      OPTPARSE_TABLE_END,
    };

    char *av[] = { "optarg",
                   "--optional-arg", "extra-args",
                   NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;

    e = optparse_add_option_table (p, opts);
    ok (e == OPTPARSE_SUCCESS, "register options");

    optind = optparse_parse_args (p, ac, av);
    ok (optind == (ac - 1), "parse options, verify optind");

    ok (optparse_hasopt (p, "optional-arg"),
        "found optional-arg option with no args");
    optarg = NULL;
    rc = optparse_getopt (p, "optional-arg", &optarg);
    ok (rc == 1, "saw --optional-arg once", rc);
    is (optarg, NULL, "no argument to --optional-arg");

    char *av2[] = { "optarg",
                   "--optional-arg=foo", "extra-args",
                   NULL };
    ac = sizeof (av2) / sizeof (av2[0]) - 1;

    optind = optparse_parse_args (p, ac, av2);
    ok (optind == (ac - 1), "parse options, verify optind");
    ok (optparse_hasopt (p, "optional-arg"),
        "found optional-arg option with args");

    rc = optparse_getopt (p, "optional-arg", &optarg);
    ok (rc == 2, "saw --optional-arg again", rc);
    is (optarg, "foo", "got argument to --optional-arg");

}

int subcmd (optparse_t *p, int ac, char **av)
{
    return (0);
}

void test_data (void)
{
    const char haha[] = "haha";
    const char hehe[] = "hehe";
    const char *s;
    optparse_t *c;
    optparse_t *p = optparse_create ("data-test");
    ok (p != NULL, "optparse_create");

    optparse_set_data (p, "foo", (void *)haha);

    s = optparse_get_data (p, "foo");
    ok (s == haha, "got back correct data");
    is (s, haha, "got back correct string");

    c = optparse_add_subcommand (p, "test", subcmd);
    ok (c != NULL, "optparse_add_subcommand");
    s = optparse_get_data (c, "foo");
    ok (s == haha, "optparse_get_data recursive lookup in parent works");
    is (s, haha, "got back correct string");

    optparse_set_data (c, "foo", (void *)hehe);
    s = optparse_get_data (c, "foo");
    ok (s == hehe, "child data overrides parent");
    is (s, hehe, "got back correct string");

    optparse_destroy (p);
}

int subcmd_one (optparse_t *p, int ac, char **av)
{
    int *ip = NULL;
    ok (p != NULL, "subcmd_one: got valid optparse structure");

    ip = optparse_get_data (p, "called");
    ok (ip != NULL, "subcmd_one: got data pointer");
    *ip = 1;
    return (0);
}

int subcmd_two (optparse_t *p, int ac, char **av)
{
    int *ip = NULL;
    ok (p != NULL, "subcmd_two: got valid optparse structure");

    ip = optparse_get_data (p, "called");
    ok (ip != NULL, "subcmd_two: got data pointer");

    *ip = optparse_get_int (p, "test-opt", 2);

    return (0);
}

int subcmd_three (optparse_t *p, int ac, char **av)
{
    int *acptr = NULL;
    ok (p != NULL, "subcmd_three: got valid optparse structure");
    acptr = optparse_get_data (p, "argc");
    ok (acptr != NULL, "subcmd_three: got argc ptr");
    *acptr = ac;

    is (av[0], "three", "subcmd_three: av[0] == %s (expected 'three')", av[0]);
    return (0);
}

int subcmd_hidden (optparse_t *p, int ac, char **av)
{
    ok (p != NULL, "subcmd_hidden: valid optparse structure");
    return (0);
}


int do_nothing (void *h, int code)
{
    return -code;
}

void test_subcommand (void)
{
    optparse_err_t e;
    int called = 0;
    int n;
    optparse_t *b;
    optparse_t *a = optparse_create ("test");

    ok (a != NULL, "optparse_create");
    b = optparse_add_subcommand (a, "one", subcmd_one);
    ok (b != NULL, "optparse_add_subcommand (subcmd_one)");
    optparse_set_data (b, "called", &called);
    ok (optparse_get_data (b, "called") == &called, "optparse_set_data ()");

    // Ensure optparse_get_parent/subcommand work:
    ok (optparse_get_parent (b) == a, "optparse_get_parent works");
    ok (optparse_get_subcommand (a, "one") == b, "optparse_get_subcommand");

    b = optparse_add_subcommand (a, "two", subcmd_two);
    ok (b != NULL, "optparse_add_subcommand (subcmd_two)");
    optparse_set_data (b, "called", &called);
    ok (optparse_get_data (b, "called") == &called, "optparse_set_data ()");

    e = optparse_set (a, OPTPARSE_LOG_FN, output_f);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (LOG_FN)");

    usage_ok (a, "\
Usage: test one [OPTIONS]\n\
   or: test two [OPTIONS]\n\
  -h, --help             Display this message.\n",
        "Usage output as expected with subcommands");

    // Set OPTPARSE_PRINT_SUBCMDS false:
    e = optparse_set (a, OPTPARSE_PRINT_SUBCMDS, 0);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (PRINT_SUBCMDS, 0)");

    usage_ok (a, "\
Usage: test [OPTIONS]...\n\
  -h, --help             Display this message.\n",
        "Usage output as expected with no print subcmds");

    e = optparse_set (b, OPTPARSE_LOG_FN, output_f);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (subcmd, LOG_FN)");

    // Add option to subcommand
    e = optparse_add_option (b, &(struct optparse_option) {
          .name = "test-opt", .key = 't', .has_arg = 1,
          .arginfo = "N",
          .usage = "Test option with numeric argument N",
        });
    ok (e == OPTPARSE_SUCCESS, "optparse_add_option");

    usage_ok (b, "\
Usage: test two [OPTIONS]...\n\
  -h, --help             Display this message.\n\
  -t, --test-opt=N       Test option with numeric argument N\n",
        "Usage output as expected with subcommands");



    char *av[] = { "test", "one", NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;

    n = optparse_parse_args (a, ac, av);
    ok (n == 1, "optparse_parse_args");
    n = optparse_run_subcommand (a, ac, av);
    ok (n >= 0, "optparse_run_subcommand");
    ok (called == 1, "optparse_run_subcommand: called subcmd_one()");

    char *av2[] = { "test", "two", NULL };
    ac = sizeof (av2) / sizeof (av2[0]) - 1;

    n = optparse_parse_args (a, ac, av2);
    ok (n == 1, "optparse_parse_args");
    n = optparse_run_subcommand (a, ac, av2);
    ok (n >= 0, "optparse_run_subcommand");
    ok (called == 2, "optparse_run_subcommand: called subcmd_two()");

    char *av3[] = { "test", "two", "--test-opt", "3", NULL };
    ac = sizeof (av3) / sizeof (av3[0]) - 1;

    // Run subcommand before parse also runs subcommand:
    //
    e = optparse_run_subcommand (a, ac, av3);
    ok (e == OPTPARSE_SUCCESS, "optparse_run_subcommand before parse succeeds");
    ok (called = 3, "optparse_run_subcmomand: called subcmd_two with correct args");

    // Test unknown option prints expected error:
    char *av4[] = { "test", "two", "--unknown", NULL };
    ac = sizeof (av4) / sizeof (av4[0]) - 1;

    e = optparse_set (b, OPTPARSE_FATALERR_FN, do_nothing);

    n = optparse_run_subcommand (a, ac, av4);
    ok (n == -1, "optparse_run_subcommand with bad args returns error");

    usage_output_is ("\
test two: unrecognized option '--unknown'\n\
Try `test two --help' for more information.\n",
    "bad argument error message is expected");

    // Test no subcommand (and subcommand required) prints error
    char *av5[] = { "test", NULL };
    ac = sizeof (av5) / sizeof (av5[0]) - 1;

    // Set OPTPARSE_PRINT_SUBCMDS true:
    e = optparse_set (a, OPTPARSE_PRINT_SUBCMDS, 1);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (PRINT_SUBCMDS, 0)");
    // Don't exit on fatal error:
    e = optparse_set (a, OPTPARSE_FATALERR_FN, do_nothing);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (FATALERR_FN, do_nothing)");
    n = optparse_run_subcommand (a, ac, av5);
    ok (n == -1, "optparse_run_subcommand with no subcommand");

    usage_output_is ("\
test: missing subcommand\n\
Usage: test one [OPTIONS]\n\
   or: test two [OPTIONS]\n\
  -h, --help             Display this message.\n",
    "missing subcommand error message is expected");

    // Add a hidden subcommand
    e = optparse_reg_subcommand (a, "hidden",
            subcmd_hidden,
            NULL,
            "This is a hidden subcmd",
            OPTPARSE_SUBCMD_HIDDEN,
            NULL);
    ok (e == OPTPARSE_SUCCESS, "optparse_reg_subcommand()");
    usage_ok (a, "\
Usage: test one [OPTIONS]\n\
   or: test two [OPTIONS]\n\
  -h, --help             Display this message.\n",
    "Hidden subcommand doesn't appear in usage output");

    // Unhide subcommand
    e = optparse_set (optparse_get_subcommand (a, "hidden"),
                      OPTPARSE_SUBCMD_HIDE, 0);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (OPTPARSE_SUBCMD_HIDE, 0)");
    usage_ok (a, "\
Usage: test hidden [OPTIONS]\n\
   or: test one [OPTIONS]\n\
   or: test two [OPTIONS]\n\
  -h, --help             Display this message.\n",
    "Unhidden subcommand now displayed in usage output");

    // Hide again with optparse_set
    e = optparse_set (optparse_get_subcommand (a, "hidden"),
                      OPTPARSE_SUBCMD_HIDE, 1);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (OPTPARSE_SUBCMD_HIDE, 1)");
    usage_ok (a, "\
Usage: test one [OPTIONS]\n\
   or: test two [OPTIONS]\n\
  -h, --help             Display this message.\n",
    "Unhidden subcommand now displayed in usage output");

    // Test Subcommand without option processing:
    optparse_t *d = optparse_add_subcommand (a, "three", subcmd_three);
    ok (d != NULL, "optparse_create()");
    e = optparse_set (d, OPTPARSE_SUBCMD_NOOPTS, 1);
    ok (e == OPTPARSE_SUCCESS, "optparse_set (OPTPARSE_SUBCMD_NOOPTS)");

    int value = 0;
    optparse_set_data (d, "argc", &value);

    char *av6[] = { "test", "three", "--help", NULL };
    ac = sizeof (av6) / sizeof (av6[0]) - 1;

    n = optparse_run_subcommand (a, ac, av6);
    ok (n == 0, "optparse_run_subcommand with OPTPARSE_SUBCMD_NOOPTS");
    ok (value == 2, "optparse_run_subcommand() run with argc = %d (expected 2)", value);
    ok (optparse_optind (d) == -1, "optparse_run_subcommand: skipped parse_args");

    optparse_destroy (a);
}

int main (int argc, char *argv[])
{

    plan (190);

    test_convenience_accessors (); /* 35 tests */
    test_usage_output (); /* 39 tests */
    test_errors (); /* 9 tests */
    test_multiret (); /* 19 tests */
    test_data (); /* 8 tests */
    test_subcommand (); /* 56 tests */
    test_long_only (); /* 13 tests */
    test_optional_argument (); /* 9 tests */

    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
