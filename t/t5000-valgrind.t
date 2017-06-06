#!/bin/sh

test_description='Run broker under valgrind with a small workload'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if ! which valgrind >/dev/null; then
    skip_all='skipping valgrind tests since no valgrind executable found'
    test_done
fi

VALGRIND=`which valgrind`
VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp
VALGRIND_WORKLOAD=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind-workload.sh
BROKER=${FLUX_BUILD_DIR}/src/broker/.libs/lt-flux-broker

if ! test -x $BROKER; then
    ${FLUX_BUILD_DIR}/src/broker/flux-broker --help >/dev/null 2>&1
fi

test_expect_success 'valgrind reports no new errors on single broker run' '
	flux ${VALGRIND} \
		--tool=memcheck \
		--leak-check=full \
		--gen-suppressions=all \
		--trace-children=no \
		--child-silent-after-fork=yes \
		--num-callers=30 \
		--leak-resolution=med \
		--error-exitcode=1 \
		--suppressions=$VALGRIND_SUPPRESSIONS \
		${BROKER} --shutdown-grace=4 ${VALGRIND_WORKLOAD} 10
'
test_done
