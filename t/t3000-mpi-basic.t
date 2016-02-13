#!/bin/sh
#

test_description='Test that Flux can launch MPI'

. `dirname $0`/sharness.sh

if test "$TEST_MPI" != "t" || ! test -x ${FLUX_BUILD_DIR}/t/mpi/hello; then
    skip_all='skipping MPI tests, MPI not available/configured'
    test_done
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

# Usage: run_program timeout ntasks nnodes
run_program() {
	local timeout=$1
	local ntasks=$2
	local nnodes=$3
	shift 3
	run_timeout $timeout flux wreckrun -l -o stdio-delay-commit \
		    -n${ntasks} -N${nnodes} $*
}

test_expect_success 'mpi hello world runs as a singleton' '
	run_program 5 1 1 ${FLUX_BUILD_DIR}/t/mpi/hello
'

test_expect_success 'mpi hello world runs on all ranks' '
	run_program 5 ${SIZE} ${SIZE} ${FLUX_BUILD_DIR}/t/mpi/hello
'

test_expect_success 'mpi hello world runs oversubscribed' '
	run_program 5 $((${SIZE}*4)) ${SIZE} ${FLUX_BUILD_DIR}/t/mpi/hello
'

test_done
