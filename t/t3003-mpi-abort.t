#!/bin/sh
#

test_description='Test that MPI_Abort works'


. `dirname $0`/sharness.sh

if test -z "$FLUX_TEST_MPI"; then
    skip_all='skipping MPI tests, FLUX_TEST_MPI not set'
    test_done
fi

if ! test -x ${FLUX_BUILD_DIR}/t/mpi/abort; then
    skip_all='skipping MPI tests, MPI not available/configured'
    test_done
fi

# Use convenient sort(1) option to determine if semantic version $1 >= $2
version_gte() {
    test "$( (echo $1; echo $2) | sort --version-sort | tail -1)" = $1
}

#
# mpich < 3.2.0 simply exits on MPI_Abort() instead of sending the PMI-1
# abort message.  Don't test this for now because of missing early exit
# detection in Flux (flux-framework/flux-core#2238)
#
mpich_min=3.2.0
mpich_ver=$(${FLUX_BUILD_DIR}/t/mpi/version | awk '/MPICH Version:/ {print $3}')
if test -n $mpich_ver && ! version_gte $mpich_ver $mpich_min; then
    skip_all="skipping MPI abort test on MPICH $mpich_ver (< $mpich_min)"
    test_done
fi

export TEST_UNDER_FLUX_CORES_PER_RANK=4
SIZE=2
MAX_MPI_SIZE=$(($SIZE*$TEST_UNDER_FLUX_CORES_PER_RANK))
test_under_flux $SIZE job

OPTS="-ocpu-affinity=off"

diag() {
	echo "test failed: cat $1"
	cat $1
	return 1
}

# These tests use ! for expected failure rather than 'test_expect_code'
# because (I think) we may alternately get the exit code passed to MPI_Abort(),
# or an exit code that indicates tasks were signaled.  If we can ensure that
# the exit code is deterministic, that should be fixed.

# get a PMI server trace in the CI output in case we need it to debug!
test_expect_success 'MPI_Abort size=1 with PMI server tracing enabled' '
	${FLUX_BUILD_DIR}/t/mpi/version | head -1 &&
	! run_timeout 60 flux mini run $OPTS -overbose=2 \
		${FLUX_BUILD_DIR}/t/mpi/abort 0
'

test_expect_success "MPI_Abort on size=${MAX_MPI_SIZE}, first rank triggers exception" '
	! run_timeout 60 flux mini run -n${MAX_MPI_SIZE} $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/abort 0 2>abort0.err &&
	(grep "exception.*MPI_Abort" abort0.err || diag abort0.err)
'

test_expect_success "MPI_Abort on size=${MAX_MPI_SIZE}, last rank triggers exception" '
	! run_timeout 60 flux mini run -n${MAX_MPI_SIZE} $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/abort $(($MAX_MPI_SIZE-1)) \
		2>abort1.err &&
	(grep "exception.*MPI_Abort" abort1.err || diag abort1.err)
'

test_done
