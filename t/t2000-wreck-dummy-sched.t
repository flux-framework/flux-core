#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE} wreck

#  Return the previous jobid
last_job_id() {
	flux wreck last-jobid
}
#  Return previous job path in kvs
last_job_path() {
	flux wreck last-jobid -p
}
test_expect_success 'load dummy sched module' '
	flux module load ${FLUX_BUILD_DIR}/t/wreck/.libs/sched-dummy.so
'
test_expect_success 'job.sumbit issues correct event' '
	$SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		-e "print (topic, msg.nnodes, msg.ncores, msg.ngpus)" \
		wreck wreck.state.submitted \
		flux submit -N2 -n8 hostname >output.submit &&
	cat <<-EOF >expected.submit &&
	wreck.state.submitted	2	8	0
	EOF
	test_debug "cat output.submit" &&
	test_cmp expected.submit output.submit
'
test_expect_success 'job.submit-nocreate issues correct event' '
	$SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		-e "print (topic, msg.nnodes, msg.ncores, msg.ngpus)" \
		wreck wreck.state.submitted \
		flux wreckrun -w submitted -N2 -n4 -g1 hostname >output.createonly &&
	cat <<-EOF >expected.createonly &&
	wreck.state.reserved	2	4	4
	wreck.state.submitted	2	4	4
	EOF
	test_debug "cat output.createonly" &&
	test_cmp expected.createonly output.createonly
'
test_expect_success 'unload dummy sched module' '
	flux module remove sched
'
test_done
