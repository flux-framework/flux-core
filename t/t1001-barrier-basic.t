#!/bin/sh
#

test_description='Test basic barrier usage in flux session

Verify basic barrier operations against a running flux comms session.
This test script verifies operation of barriers and should be run
before other tests that depend on barriers.
'

. `dirname $0`/sharness.sh
test_under_flux 4

test_expect_success 'barrier: returns when complete' '
	${FLUX_BUILD_DIR}/src/test/tbarrier --nprocs 1 abc
'

test_expect_success 'barrier: blocks while incomplete' '
	test_expect_code 142 run_timeout 1 \
	  ${FLUX_BUILD_DIR}/src/test/tbarrier --nprocs 2 xyz
'

test_expect_success 'barrier: fails with name=NULL outside of LWJ' '
	unset FLUX_LWJ_ID
	unset SLURM_STEPID
	test_expect_code 1 \
	  ${FLUX_BUILD_DIR}/src/test/tbarrier --nprocs 1
'

test_expect_success 'barrier: succeeds with name=NULL inside LWJ' '
	unset SLURM_STEPID
        FLUX_LWJ_ID=1; export FLUX_LWJ_ID
	${FLUX_BUILD_DIR}/src/test/tbarrier --nprocs 1
'

test_expect_success 'barrier: succeeds with name=NULL inside SLURM step' '
	unset FLUX_LWJ_ID
        SLURM_STEPID=1; export SLURM_STEPID
	${FLUX_BUILD_DIR}/src/test/tbarrier --nprocs 1
'

test_done
