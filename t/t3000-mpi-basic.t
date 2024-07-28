#!/bin/sh
#

test_description='Test that Flux can launch MPI'

. `dirname $0`/sharness.sh

if test -z "$FLUX_TEST_MPI"; then
    skip_all='skipping MPI tests, FLUX_TEST_MPI not set'
    test_done
fi

HELLO=${FLUX_BUILD_DIR}/t/mpi/hello
if ! test -x ${HELLO}; then
    skip_all='skipping MPI tests, MPI not available/configured'
    test_done
fi

# work around https://github.com/open-mpi/ompi/issues/7701 and similar in mpich
export HWLOC_COMPONENTS=-gl
export TEST_UNDER_FLUX_CORES_PER_RANK=4
SIZE=2
MAX_MPI_SIZE=$(($SIZE*$TEST_UNDER_FLUX_CORES_PER_RANK))
test_under_flux $SIZE job

OPTS="-ocpu-affinity=off -overbose=1"

test_expect_success 'show MPI version under test' '
	${FLUX_BUILD_DIR}/t/mpi/version
'

test_expect_success 'mpi hello various sizes' '
	run_timeout 30 flux submit --cc=1-$MAX_MPI_SIZE $OPTS \
		--watch -n{cc} ${HELLO} >hello.out &&
	for i in $(seq 1 $MAX_MPI_SIZE); do \
		grep "There are $i tasks" hello.out; \
	done
'

# issue 3649 - try to get two size=2 jobs running concurrently on one broker
test_expect_success 'mpi hello size=2 concurrent submit of 8 jobs' '
	run_timeout 30 flux submit --cc=1-8 $OPTS \
		--watch -n2 ${HELLO} >hello2.out &&
	test $(grep "There are 2 tasks" hello2.out | wc -l) -eq 8
'

# Run some basic tests pulled from MPICH as a sanity check
test_expect_success 'ANL self works' '
	run_timeout 30 flux run $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/self
'
test_expect_success 'ANL simple works' '
	run_timeout 30 flux run -n 2 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/simple
'
test_expect_success 'ANL sendrecv works on 1 node' '
	run_timeout 30 flux run -n 2 -N1 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/sendrecv
'
test_expect_success 'ANL sendrecv works on 2 nodes' '
	run_timeout 30 flux run -n 2 -N2 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/sendrecv
'
test_expect_success LONGTEST 'ANL netpipe works' '
	flux run -n 2 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/netpipe
'
test_expect_success 'ANL patterns works 1 node' '
	run_timeout 30 flux run -n 2 -N1 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/patterns
'
test_expect_success 'ANL patterns works 2 nodes' '
	run_timeout 30 flux run -n 2 -N2 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/patterns
'
test_expect_success LONGTEST 'ANL adapt works' '
	flux run -n 3 $OPTS \
		${FLUX_BUILD_DIR}/t/mpi/mpich_basic/adapt
'


test_done
