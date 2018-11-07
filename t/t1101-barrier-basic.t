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

test_expect_success 'barrier: fails with name=NULL outside of LWJ' '
	unset FLUX_JOB_ID &&
	unset SLURM_STEPID &&
	test_expect_code 1 ${tbarrier} --nprocs 1
'

test_expect_success 'barrier: succeeds with name=NULL inside LWJ' '
	unset SLURM_STEPID &&
        FLUX_JOB_ID=1 && export FLUX_JOB_ID &&
	flux exec -n ${tbarrier} --nprocs ${SIZE}
'

test_expect_success 'barrier: succeeds with name=NULL inside SLURM step' '
	unset FLUX_LWJ_ID &&
        SLURM_STEPID=1 && export SLURM_STEPID &&
	flux exec -n ${tbarrier} --nprocs ${SIZE}
'

test_expect_success 'barrier: remove barrier module' '
	flux module remove -r all barrier
'


test_done
