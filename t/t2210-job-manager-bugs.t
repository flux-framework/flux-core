#!/bin/sh

test_description='Regression tests for job-manager bugs'

. $(dirname $0)/sharness.sh

test_under_flux 1

flux setattr log-stderr-level 1

#
# Issue 2664 job-manager: counting error
#

test_expect_success 'issue2664: start three jobs ' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	echo Requesting ${ncores} cores &&
	flux mini submit -n ${ncores} sleep 3600 >job1.out &&
	flux mini submit hostname >job2.out &&
	flux mini submit hostname >job3.out
'
# canceling job that has not yet sent alloc to scheduler improperly
# decrements count of outstanding alloc requests
test_expect_success 'issue2664: cancel job 3' '
	flux job cancel $(cat job3.out)
'
# Next job submitted triggers another alloc request when ther are no
# more slots, which triggers error response from scheduler that is fatal
# to job manager.
test_expect_success 'issue2664: submit job 4' '
	flux mini submit hostname >job4.out
'
# Hangs here (hitting timeout) when bug is present
test_expect_success 'issue2664: cancel job 1 and drain (cleanup)' '
	flux job cancel $(cat job1.out) &&
	run_timeout 5 flux queue drain
'

#
# Issue 3051 job-manager: segfault on priority change with pending alloc
#

test_expect_success 'issue3051: submit full system job' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux mini submit -n ${ncores} sleep 3600 >issue3051.job1
'
test_expect_success 'issue3051: submit one more job and wait for alloc' '
	flux mini submit --flags=debug /bin/true >issue3051.job2 &&
	flux job wait-event -t 5 $(cat issue3051.job2) debug.alloc-request
'
test_expect_success 'issue3051: cannot reprioritize job with pending alloc' '
	test_must_fail flux job priority $(cat issue3051.job2) 0 2>issue3051.err
'
test_expect_success 'issue3051: human message is reasonable' '
	grep alloc issue3051.err
'
test_expect_success 'issue3051: clean up' '
	flux job cancel $(cat issue3051.job2) &&
	flux job cancel $(cat issue3051.job1) &&
	flux queue drain
'

#
# Issue 3218 job-manager: segfault when changing priority of running job
#

test_expect_success 'issue3218: priority change on running job doesnt segfault' '
        id=$(flux mini submit sleep 600) &&
        flux job wait-event $id start &&
        test_must_fail flux job priority $id 0 &&
        flux job cancel $id
'

test_done
