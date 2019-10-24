#!/bin/sh

test_description='check for responses from sched to outstanding requests'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

hwloc_by_rank='{"0-1": {"Core": 2, "cpuset": "0-1"}}'
RPC=${FLUX_BUILD_DIR}/t/request/rpc
SCHED_DUMMY=${FLUX_BUILD_DIR}/t/job-manager/.libs/sched-dummy.so
REQ_AND_UNLOAD="flux python ${SHARNESS_TEST_SRCDIR}/schedutil/req_and_unload.py"

test_expect_success 'schedutil: load sched-dummy --cores=2' '
	flux module load -r 0 ${SCHED_DUMMY} --cores=2
'

test_expect_success 'schedutil: set bit to hang alloc/free requests' '
	flux module debug --setbit 0x8000 sched-dummy
'

test_expect_success 'schedutil: alloc/free fail with ENOSYS(38) after unload' '
    ${REQ_AND_UNLOAD} sched-dummy
'

test_expect_success 'schedutil: load default by_rank' '
	flux kvs put resource.hwloc.by_rank="$(echo $hwloc_by_rank)" &&
	flux kvs get resource.hwloc.by_rank
'

test_expect_success 'schedutil: successfully loaded sched-simple' '
	flux module load -r 0 sched-simple
'

test_expect_success 'schedutil: set bit to hang alloc/free requests' '
	flux module debug --setbit 0x8000 sched-simple
'

test_expect_success 'schedutil: alloc/free fail with ENOSYS(38) after unload' '
    ${REQ_AND_UNLOAD} sched-simple
'

test_done
