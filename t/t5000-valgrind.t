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
VALGRIND_NBROKERS=${VALGRIND_NBROKERS:-2}
VALGRIND_SHUTDOWN_GRACE=${VALGRIND_SHUTDOWN_GRACE:-16}

test_expect_success \
  "valgrind reports no new errors on $VALGRIND_NBROKERS broker run" '
	run_timeout 120 \
	flux start -s ${VALGRIND_NBROKERS} --wrap=libtool,e,${VALGRIND} \
		--wrap=--tool=memcheck \
		--wrap=--leak-check=full \
		--wrap=--gen-suppressions=all \
		--wrap=--trace-children=no \
		--wrap=--child-silent-after-fork=yes \
		--wrap=--num-callers=30 \
		--wrap=--leak-resolution=med \
		--wrap=--error-exitcode=1 \
		--wrap=--suppressions=$VALGRIND_SUPPRESSIONS \
		-o,--shutdown-grace=${VALGRIND_SHUTDOWN_GRACE} \
		 ${VALGRIND_WORKLOAD}
'
test_done
