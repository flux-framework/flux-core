Core Flux Tests
===============

This directory contains many basic functionality and
regression test scripts for flux-core commands and utilities.
The main test framework for running these tests is built using a
slightly modified version of the [sharness] project, which itself
is derived from the Git project's testsuite.

Writing tests using sharness is described immediately below.

Tests may also be written in Lua using a small Lua module
`fluxometer.lua` which enables tests to rerun themselves under
flux session, and have access to the [lua-TestMore] TAP framework.
This module is documented below in the section Lua Tests.

Shell-based Scripts
===================

Tests are written as small shell scripts that assert behavior
of flux core commands using the functions found in `sharness.sh`.
The tests output in [TAP] (Test Anything Protocol), and thus
may be run under any TAP harness.

Running tests
=============

Tests may be run in as many as 3 different ways, the easiest
of which is to issue `make check` from this directory or at
the top-level flux-core build directory.

Some systems may have poor performance running `make -j N check`
due to hwloc topology discovery occurring in parallel across many
tests, each of which may start multiple brokers per test. To
alleviate this issue, Flux may be directed to read topology from
an XML file with the `FLUX_HWLOC_XMLFILE` environment variable,
which avoids most dynamic topology discovery for the entire
testsuite, e.g.

```
$ hwloc-ls --of xml >machine.xml
$ FLUX_HWLOC_XMLFILE=$(pwd)/machine.xml make -j 32 check
```

The tests may also all be invoked via the `./runtests.sh` script,
which runs all tests in turn and aggregates results of all tests
at completion.  Finally, since the tests output TAP, they may be run
through a TAP harness such as the [prove] command, e.g.

```
$ prove --timer ./t*.t
[07:13:52] ./t0000-sharness.t .......... ok      557 ms
[07:13:53] ./t0001-basic.t ............. ok     3331 ms
[07:13:56] ./t0002-basic-in-session.t .. ok     2523 ms
[07:13:59] ./t1001-kvs.t ............... ok     4048 ms
[07:14:03]
All tests successful.
Files=4, Tests=63, 11 wallclock secs ( 0.03 usr  0.00 sys +  3.34 cusr  2.26 csys =  5.63 CPU)
Result: PASS
```

Test scripts may also be run individually, as in:

```
$ ./t0001-basic.t
ok 1 - TEST_NAME is set
ok 2 - run_timeout works
ok 3 - we can find a flux binary
ok 4 - flux-keygen works
ok 5 - path to broker is sane
ok 6 - flux-start works
# passed all 6 test(s)
1..6
```

All tests support a standard set of options:

```
 -v, --verbose
        This makes the test more verbose.  Specifically, the
        command being run and their output if any are also
        output.

 -d, --debug
        This may help the person who is developing a new test.
        It causes any commands defined with `test_debug` to run.
        The "trash" directory (used to store all temporary data
        during testing) is not deleted even if there are no
        failed tests so that you can inspect its contents after
        the test finished.

 -i, --immediate
        This causes the test to immediately exit upon the first
        failed test. Cleanup commands requested with
        test_when_finished are not executed if the test failed,
        in order to keep the state for inspection by the tester
        to diagnose the bug.

 -l, --long-tests
        This causes additional long-running tests to be run (where
        available), for more exhaustive testing.

 --tee
        In addition to printing the test output to the terminal,
        write it to files named `t/test-results/$TEST_NAME.out`.
        As the names depend on the tests' file names, it is safe to
        run the tests with this option in parallel.

 --root=<directory>
        Create "trash" directories used to store all temporary data during
        testing under <directory>, instead of the t/ directory.
        Using this option with a RAM-based filesystem (such as tmpfs)
        can massively speed up the test suite.

```

Normally long running tests are not executed.  The environment
variable `TEST_LONG` may be set to have all long running tests run.

The environment variable `FLUX_TEST_INSTALLED_PATH` may also be set
to the path to an *installed* version of the `flux(1)` command, for
running the testsuite against an installed version of flux-core.

The environment variable `FLUX_TEST_VALGRIND` may be set to `t`
to run tests under valgrind.

The environment variable `FLUX_TESTS_LOGFILE` may be set to `t` to force
most tests to generate a verbose log as `$TEST_NAME.output` when debugging
tests.


Skipping Tests
--------------

The environment variable `SKIP_TESTS` is a space separated
list of *patterns* that tells which tests to skip, and can either
match the test number `t[0-]{4}` to skip an entire test script,
or have an appended `.$number` to skip test `$number` in the
matching test script.

Test Naming
-----------

The test files are by convention named

    tNNNN-<name>.sh

where N is a decimal digit.

Writing Tests
-------------

Tests for most part are written with the simple [API] exported
by sharness. See `sharness.sh` in this directory for the details.
Each test script is written as a shell script and should start
with the standard `#!/bin/sh` to allow running tests directly.
After copyright notices, etc, the test script should assign
to the variable `test_description`, like this:

```
test_description='Test basic commands functionality under a Flux instance

Ensure the very basics of flux commands work.
This suite verifies functionality that may be assumed working by
other tests.
'
```

After assignment of `test_description`, the test script must
source the sharness library via `sharness.sh`, like this

```
. `dirname $0`/sharness.sh
```

> NOTE: the `\`dirname $0\`` is used to ensure sharness.sh is sourced
> from the same directory as the test script itself.

the sharness library sets up a trash directory for the test and
does a `chdir(2)` into it. It then defines a set of standard helper
functions for the script to use. These functions are designed to
make all scripts behave consistently with regard to test output,
command line arguments, etc.

The final line of the test script should issue the `test_done`
function, like this

```
test_done
```

The `test_done` function may also be used to exit early from a
test script. Do not use `exit` to exit from a test, as this will
cause the entire test harness to abort.

Extension functions
-------------------

The flux-core testsuite extends the sharness [API] with the
following extra functions:

```
  test_under_flux <size> :
	Re-invokes the test library under a flux instance of
	size <size>. If size is not given a default of 1 is used.
	This function essentially invokes

	  exec flux start --test-size=N /path/to/test/script args...

  run_timeout S COMMAND... :
	Runs COMMAND with timeout of S seconds. run_timeout will
	exit with non-zero status if COMMAND does not exit normally
	before S seconds. Otherwise it returns with the exit status
	of COMMAND.

```
Helper scripts
--------------

Utility and other helper scripts which are specific to the testsuite
should be placed in the `./scripts` directory. This directory can
be referenced as `$SHARNESS_TEST_SRCDIR/scripts` to run scripts
directly, or individual tests can add this path to their `PATH`.

Lua Tests
=========

Lua tests should be bootstrapped by loading the `fluxometer` module
as the first line of the script, and initializing a test object
with the `init` function. For example:

```
local t = require 'fluxometer'.init (...)
```

If the test should be run within a flux instance, then the
`start_session` method of the test object can be called, with
a table as the only parameter.

```
t:start_session {}
```

The table argument to `start_session` has the optional parameters:

 * `size`:    size of flux session to start
 * `args`:    table of extra arguments to pass to `flux-start`

Example:

```
local t = require 'fluxometer'.init (...)
t:start_session { size = 4 }
```

Once `fluxometer` is loaded, the script will have access to Lua
`Test.More` functions. The script can also make use of the test
object methods `say()` to print diagnostics, and `die()` to
terminate the tests with failure.

--
[sharness]: https://github.com/chriscool/sharness
[API]: https://github.com/chriscool/sharness/blob/master/API.md
[TAP]: http://testanything.org
[prove]: http://linux.die.net/man/1/prove
[lua-TestMore]: https://fperrad.frama.io/lua-TestMore/
