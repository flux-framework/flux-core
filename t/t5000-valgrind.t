#!/bin/sh

test_description='Run broker under valgrind with a small workload'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

#  Do not run valgrind test by default unless FLUX_ENABLE_VALGRIND_TEST
#   is set in environment (e.g. by CI), or the test run run with -d, --debug
#
if test -z "$FLUX_ENABLE_VALGRIND_TEST" && test "$debug" = ""; then
    skip_all='skipping valgrind tests since FLUX_ENABLE_VALGRIND_TEST not set'
    test_done
fi
if ! which valgrind >/dev/null; then
    skip_all='skipping valgrind tests since no valgrind executable found'
    test_done
fi
if ! test_have_prereq NO_ASAN; then
    skip_all='skipping valgrind tests since AddressSanitizer is active'
    test_done
fi

# Do not run test by default unless valgrind/valgrind.h was found, since
#  this has been known to introduce false positives (#1097). However, allow
#  run to be forced on the cmdline with -d, --debug.
#
have_valgrind_h() {
    grep -q "^#define HAVE_VALGRIND 1" ${FLUX_BUILD_DIR}/config/config.h
}
if ! have_valgrind_h && test "$debug" = ""; then
    skip_all='skipping valgrind tests b/c valgrind.h not found. Use -d, --debug to force'
    test_done
fi

VALGRIND=`which valgrind`
VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp
VALGRIND_WORKLOAD=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind-workload.sh

VALGRIND_NBROKERS=${VALGRIND_NBROKERS:-2}

# Allow jobs to run under sdexec in test, if available
cat >valgrind.toml <<-EOT
[exec]
service-override = true
EOT

test_expect_success \
  "valgrind reports no new errors on $VALGRIND_NBROKERS broker run" '
	run_timeout 300 \
	flux start -s ${VALGRIND_NBROKERS} \
		--test-exit-timeout=120 \
		--config-path=valgrind.toml \
		--wrap=libtool,e,${VALGRIND} \
		--wrap=--tool=memcheck \
		--wrap=--leak-check=full \
		--wrap=--gen-suppressions=all \
		--wrap=--trace-children=no \
		--wrap=--child-silent-after-fork=yes \
		--wrap=--num-callers=30 \
		--wrap=--leak-resolution=med \
		--wrap=--error-exitcode=1 \
		--wrap=--suppressions=$VALGRIND_SUPPRESSIONS \
		 ${VALGRIND_WORKLOAD}
'
test_done
