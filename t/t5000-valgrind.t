#!/bin/sh

test_description='Run broker under valgrind with a small workload'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if ! which valgrind >/dev/null; then
    skip_all='skipping valgrind tests since no valgrind executable found'
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

export FLUX_PMI_SINGLETON=1 # avoid finding leaks in slurm libpmi.so

VALGRIND=`which valgrind`
VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp
VALGRIND_WORKLOAD=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind-workload.sh
BROKER=${FLUX_BUILD_DIR}/src/broker/flux-broker

# broker run under valgrind may need extra retries in flux_open():
export FLUX_LOCAL_CONNECTOR_RETRY_COUNT=10

test_expect_success 'valgrind reports no new errors on single broker run' '
	run_timeout 120 \
	libtool e flux ${VALGRIND} \
		--tool=memcheck \
		--leak-check=full \
		--gen-suppressions=all \
		--trace-children=no \
		--child-silent-after-fork=yes \
		--num-callers=30 \
		--leak-resolution=med \
		--error-exitcode=1 \
		--suppressions=$VALGRIND_SUPPRESSIONS \
		${BROKER} --shutdown-grace=16 ${VALGRIND_WORKLOAD} 10
'
test_done
