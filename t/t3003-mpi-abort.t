#!/bin/sh
#

test_description='Test that MPI_Abort works'

# MPI behaviors WRT to MPI_Abort():
#
# mpich
#   In versions < 3.2.0, MPI_Abort() prints error and exits from the task.
#   In versions >= 3.2.0, MPI_Abort() sends a PMI-1 abort command using its
#   built-in PMI-1 wire protocol client.
# openmpi
#   In Openmpi version 4, the flux MCA plugin dlopens the flux libpmi.so
#   and calls PMI_Abort().  The current implementation sends a PMI-1 abort
#   command.
# mvapich
#   In version 2.3.6, MPI_Abort() prints error and exits from the task.
#   Other versions not checked.
#
# N.B. After an MPI_Abort(), the job might exit with
# 1) the MPI_Abort() exit code
# 2) (128 + sig) if other shells have to be cleaned up

. `dirname $0`/sharness.sh

if test -z "$FLUX_TEST_MPI"; then
    skip_all='skipping MPI tests, FLUX_TEST_MPI not set'
    test_done
fi

if ! test -x ${FLUX_BUILD_DIR}/t/mpi/abort; then
    skip_all='skipping MPI tests, MPI not available/configured'
    test_done
fi

# work around https://github.com/open-mpi/ompi/issues/7701 and similar in mpich
export HWLOC_COMPONENTS=-gl
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

test_expect_success 'show MPI version under test' '
        ${FLUX_BUILD_DIR}/t/mpi/version
'

# These tests use ! for expected failure rather than 'test_expect_code'
# because the job exit code is not deterministic.  'test_must_fail' cannot be
# used either because 128 + SIGNUM is not accepted as "failure".

# get a PMI server trace in the CI output in case we need it to debug!
test_expect_success 'MPI_Abort size=1 with PMI server tracing enabled' '
	${FLUX_BUILD_DIR}/t/mpi/version | head -1 &&
	! run_timeout 60 flux run $OPTS -overbose=2 \
		${FLUX_BUILD_DIR}/t/mpi/abort 0
'

test_expect_success "MPI_Abort on size=${MAX_MPI_SIZE}, first rank triggers exception" '
	! run_timeout 60 flux run -n${MAX_MPI_SIZE} $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/abort 0 2>abort0.err &&
	(grep exception abort0.err || diag abort0.err) &&
	test_debug "cat abort0.err" &&
	grep "Rank 0 is going to MPI_Abort now" abort0.err
'

test_expect_success "MPI_Abort on size=${MAX_MPI_SIZE}, last rank triggers exception" '
	rank=$(($MAX_MPI_SIZE-1)) &&
	! run_timeout 60 flux run -n${MAX_MPI_SIZE} $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/abort $rank \
		2>abort1.err &&
	(grep exception abort1.err || diag abort1.err) &&
	grep "Rank $rank is going to MPI_Abort now" abort1.err
'

test_done
