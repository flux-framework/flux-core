#!/bin/sh
#

test_description='Test basic barrier usage in flux session

Verify basic barrier operations against a running flux comms session.
This test script verifies operation of barriers and should be run
before other tests that depend on barriers.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal
tbarrier="${FLUX_BUILD_DIR}/t/barrier/tbarrier"
test "$verbose" = "t" || tbarrier="${tbarrier} -q"

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'barrier: load barrier module' '
	flux module load -r all barrier
'

test_expect_success 'barrier: returns when complete' '
	${tbarrier} --nprocs 1 abc
'

test_expect_success 'barrier: returns when complete (all ranks)' '
	flux exec -n ${tbarrier} --nprocs ${SIZE} abc
'

test_expect_success 'barrier: blocks while incomplete' '
	test_expect_code 142 run_timeout 1 \
	  ${tbarrier} --nprocs 2 xyz
'

test_expect_success 'barrier: fails with name=NULL outside of job' '
	unset FLUX_JOB_ID &&
	unset SLURM_STEPID &&
	test_expect_code 1 ${tbarrier} --nprocs 1
'

test_expect_success 'barrier: succeeds with name=NULL inside Flux job' '
	unset SLURM_STEPID &&
        FLUX_JOB_ID=1 && export FLUX_JOB_ID &&
	flux exec -n ${tbarrier} --nprocs ${SIZE}
'

test_expect_success 'barrier: succeeds with name=NULL inside SLURM step' '
	unset FLUX_LWJ_ID &&
        SLURM_STEPID=1 && export SLURM_STEPID &&
	flux exec -n ${tbarrier} --nprocs ${SIZE}
'
test_expect_success 'enter request with empty payload fails with EPROTO(71)' '
	${RPC} barrier.enter 71 </dev/null
'

test_expect_success 'barrier: works with guest user' '
	newid=$(($(id -u)+1)) &&
	FLUX_HANDLE_ROLEMASK=0x02 FLUX_HANDLE_USERID=${newid} \
		${tbarrier} --nprocs 1 guest-test
'

test_expect_success 'barrier: disconnect destroys barrier' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		barrier barrier.exit \
                "${tbarrier} --nprocs 2 --early-exit discon" >discon.out &&
	grep barrier.exit discon.out
'
test_expect_success 'barrier: disconnect destroys guest barrier' '
	newid=$(($(id -u)+1)) &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		barrier barrier.exit \
		"FLUX_HANDLE_ROLEMASK=0x02 FLUX_HANDLE_USERID=${newid} \
                ${tbarrier} --nprocs 2 --early-exit gdiscon" >gdiscon.out &&
	grep barrier.exit gdiscon.out
'

test_expect_success 'barrier: remove barrier module' '
	flux module remove -r all barrier
'


test_done
