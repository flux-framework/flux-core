#!/bin/sh

test_description='Test flux job exec hello protocol with multiple providers'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux setattr log-stderr-level 1
hwloc_fake_config='{"0-1":{"Core":2,"cpuset":"0-1"}}'
execservice=${SHARNESS_TEST_SRCDIR}/job-manager/exec-service.lua

lastevent() { flux job eventlog $1 | awk 'END{print $2}'; }

test_expect_success 'exec hello: load sched-simple module' '
	#  Add fake by_rank configuration to kvs:
	flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
	flux module load -r 0 sched-simple
'
test_expect_success NO_CHAIN_LINT 'exec hello: start exec server-in-a-script' '
	${execservice} test-exec > server1.log &
	SERVER1=$! &&
	id=$(flux jobspec srun hostname | flux job submit --flags=debug) &&
	flux job wait-event ${id} clean &&
	test_debug "cat server1.log" &&
	grep "start: ${id}" server1.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: takeover: start new service' '
	${execservice} test-exec2 > server2.log &
	SERVER2=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec2 &&
	id=$(flux jobspec srun hostname | flux job submit --flags=debug) &&
	flux job wait-event ${id} clean &&
	grep "start: ${id}" server2.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: teardown existing servers' "
	kill -9 ${SERVER1} && kill -9 ${SERVER2} &&
	wait ${SERVER1} || : &&
	wait ${SERVER2} || : &&
	flux module list > module.list &&
	test_must_fail grep test-exec module.list
"
test_expect_success NO_CHAIN_LINT 'exec hello: job start events are paused' '
	id=$(flux jobspec srun hostname | flux job submit --flags=debug) &&
	flux job wait-event -vt 5 ${id} alloc &&
	test_debug "flux job eventlog ${id}" &&
	test $(lastevent ${id}) = "debug.start-lost"
'
test_expect_success NO_CHAIN_LINT 'exec hello: start server with job timer' '
	${execservice} test-exec3 30 > server3.log &
	SERVER3=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec3
'
test_expect_success NO_CHAIN_LINT 'exec hello: paused job now has start event' '
	flux job wait-event -t 2 ${id} start &&
	test_debug "cat server3.log" &&
	grep "test-exec3: start: ${id}" server3.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: hello now returns error due to running job' '
	run_timeout 5 test_must_fail ${execservice} testexecfoo
'
test_expect_success NO_CHAIN_LINT 'exec hello: terminate all jobs and servers' '
	flux job cancel ${id} &&
	test_debug "cat server3.log" &&
	flux job wait-event -t 2.5 ${id} clean &&
	kill ${SERVER3}
'
test_expect_success 'job-exec: remove sched-simple module' '
	flux module remove -r 0 sched-simple
'

test_done
