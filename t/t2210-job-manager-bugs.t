#!/bin/sh

test_description='Regression tests for job-manager bugs'

. $(dirname $0)/sharness.sh

test_under_flux 1

flux setattr log-stderr-level 1

#
# Issue 2664 job-manager: counting error
#

test_expect_success 'issue2664: start three jobs ' '
	echo Requesting $(nproc) cores &&
	flux mini submit -n $(nproc) sleep 3600 >job1.out &&
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
# Hangs here (hitting timeout)
test_expect_success 'issue2664: cancel job 1 and drain (cleanup)' '
	flux job cancel $(cat job1.out) &&
	run_timeout 5 flux queue drain
'

test_done
