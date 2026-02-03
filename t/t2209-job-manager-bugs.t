#!/bin/sh


test_description='Regression tests for job-manager bugs'

. $(dirname $0)/sharness.sh

test_under_flux 1 full -Slog-stderr-level=1

RPC=${FLUX_BUILD_DIR}/t/request/rpc

#
# Issue 2664 job-manager: counting error
#

test_expect_success 'issue2664: start three jobs ' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	echo Requesting ${ncores} cores &&
	flux submit -n ${ncores} sleep 3600 >job1.out &&
	flux submit hostname >job2.out &&
	flux submit hostname >job3.out
'
# canceling job that has not yet sent alloc to scheduler improperly
# decrements count of outstanding alloc requests
test_expect_success 'issue2664: cancel job 3' '
	flux cancel $(cat job3.out)
'
# Next job submitted triggers another alloc request when there are no
# more slots, which triggers error response from scheduler that is fatal
# to job manager.
test_expect_success 'issue2664: submit job 4' '
	flux submit hostname >job4.out
'
# Hangs here (hitting timeout) when bug is present
test_expect_success 'issue2664: cancel job 1 and drain (cleanup)' '
	flux cancel $(cat job1.out) &&
	run_timeout 5 flux queue drain
'

#
# Issue 3218 job-manager: segfault when changing urgency of running job
#

test_expect_success 'issue3218: urgency change on running job doesnt segfault' '
        id=$(flux submit sleep 600) &&
        flux job wait-event $id start &&
        test_must_fail flux job urgency $id 0 &&
        flux cancel $id
'
#
# Issue 4409: eventlog commit / job start race
# Also tests job-manager.set-batch-timeout RPC
#
test_expect_success 'issue4409: eventlog commit races with job launch' '
	printf "{\"timeout\": \"1\"}" | \
	    test_expect_code 1 ${RPC} job-manager.set-batch-timeout &&
	printf "{\"timeout\": 1}" | ${RPC} job-manager.set-batch-timeout &&
	flux submit -vvv --cc=1-5 --wait --quiet hostname &&
	printf "{\"timeout\": 0.01}" | ${RPC} job-manager.set-batch-timeout
'
test_done
