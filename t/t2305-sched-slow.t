#!/bin/sh

test_description='test a scheduler that is slow to respond
'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 1

# Usage: module_debug_defer modname True|False
module_debug_defer () {
	flux python -c "import flux; flux.Flux().rpc(\"module.debug\",{\"name\":\"$1\",\"defer\":$2}).get()"
}

test_expect_success 'pause sched message handling' '
	module_debug_defer sched-simple True
'
test_expect_success 'a job can be submitted when the scheduler is unresponsive' '
	flux submit -N1 \
	    --flags=debug --wait-event=debug.alloc-request \
	    true >job1.id
'
test_expect_success 'the job is blocked on the alloc request' '
	test_expect_code 137 run_timeout 2 \
	    flux job wait-event $(cat job1.id) alloc
'
test_expect_success 'unpause sched message handling' '
	module_debug_defer sched-simple False
'
test_expect_success 'the job gets its allocation and completes' '
	run_timeout 30 flux job wait-event $(cat job1.id) clean
'
test_expect_success 'submit a job and wait for it to get an alloc response' '
	flux submit -N1 --wait-event=alloc sleep 3600 >job2.id
'
test_expect_success 'pause sched message handling' '
	module_debug_defer sched-simple True
'
test_expect_success 'cancel the job' '
	flux cancel $(cat job2.id)
'
test_expect_success 'the job is able to get through CLEANUP state' '
	run_timeout 30 flux job wait-event $(cat job2.id) clean
'
test_expect_success 'unpause sched message handling' '
	module_debug_defer sched-simple False
'
test_expect_success 'another -N1 job can run so resources are free' '
	run_timeout 30 flux run -N1 true
'
test_expect_success 'drain the only node in the instance' '
	flux resource drain 0
'
test_expect_success 'submit a job' '
	flux submit -N1 \
	    --flags=debug --wait-event=debug.alloc-request \
	    true >job3.id
'
test_expect_success 'the job is blocked on the alloc request' '
	test_expect_code 137 run_timeout 2 \
	    flux job wait-event $(cat job3.id) alloc
'
test_expect_success 'pause sched message handling' '
	module_debug_defer sched-simple True
'
test_expect_success 'cancel the job' '
	flux cancel $(cat job3.id)
'
test_expect_success 'the job is able to get through CLEANUP state' '
	run_timeout 30 flux job wait-event $(cat job3.id) clean
'
test_expect_success 'unpause sched message handling' '
	module_debug_defer sched-simple False
'
test_expect_success 'undrain the node' '
	flux resource undrain 0
'
test_expect_success 'another -N1 job can run so everything still works!' '
	run_timeout 30 flux run -N1 true
'

test_done
