#!/bin/sh
#
# ci=asan

test_description='Test flux job exec hello protocol with multiple providers'

. $(dirname $0)/sharness.sh

test_under_flux 1 job -Slog-stderr-level=1

execservice=${SHARNESS_TEST_SRCDIR}/job-manager/exec-service.lua

lastevent() { flux job eventlog $1 | awk 'END{print $2}'; }

test_expect_success 'exec hello: remove job-exec module' '
	flux module remove job-exec
'
test_expect_success NO_CHAIN_LINT 'exec hello: start exec server-in-a-script' '
	${execservice} test-exec > server1.log &
	SERVER1=$! &&
	id=$(flux submit --flags=debug hostname) &&
	flux job wait-event ${id} clean &&
	test_debug "cat server1.log" &&
	grep "start: $(flux job id ${id})" server1.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: takeover: start new service' '
	${execservice} test-exec2 > server2.log &
	SERVER2=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec2 &&
	id=$(flux submit --flags=debug hostname) &&
	flux job wait-event ${id} clean &&
	grep "start: $(flux job id ${id})" server2.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: teardown existing servers' "
	kill -9 ${SERVER1} && kill -9 ${SERVER2} &&
	wait ${SERVER1} || : &&
	wait ${SERVER2} || : &&
	flux module list > module.list &&
	test_must_fail grep test-exec module.list
"
test_expect_success NO_CHAIN_LINT 'exec hello: job start events are paused' '
	id=$(flux submit --flags=debug hostname) &&
	flux job wait-event -vt 5 ${id} alloc &&
	test_debug "flux job eventlog ${id}" &&
	test $(lastevent ${id}) = "alloc"
'
test_expect_success NO_CHAIN_LINT 'exec hello: start server with job timer' '
	${execservice} test-exec3 30 > server3.log &
	SERVER3=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec3
'
test_expect_success NO_CHAIN_LINT 'exec hello: paused job now has start event' '
	flux job wait-event -t 2 ${id} start &&
	test_debug "cat server3.log" &&
	grep "test-exec3: start: $(flux job id ${id})" server3.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: hello now returns error due to running job' '
	test_expect_code 1 run_timeout 5 ${execservice} testexecfoo
'
test_expect_success NO_CHAIN_LINT 'exec hello: terminate all jobs and servers' '
	flux cancel ${id} &&
	test_debug "cat server3.log" &&
	flux job wait-event -t 2.5 ${id} clean &&
	kill ${SERVER3}
'
test_expect_success NO_CHAIN_LINT 'exec hello: start server for disconnect test' '
	${execservice} test-exec5 30 > server5.log &
	SERVER5=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec5 &&
	id5=$(flux submit --flags=debug hostname) &&
	flux job wait-event -t 5 ${id5} start
'
test_expect_success NO_CHAIN_LINT 'exec hello: removing service posts start-lost' '
	kill -9 ${SERVER5} &&
	wait ${SERVER5} || : &&
	flux job wait-event -t 5 ${id5} debug.start-lost &&
	test_debug "flux job eventlog ${id5}" &&
	flux job wait-event -t 5 --match-context=note=disconnect \
		${id5} debug.start-lost
'
test_expect_success NO_CHAIN_LINT 'exec hello: new service reattaches lost job' '
	${execservice} test-exec6 30 > server6.log &
	SERVER6=$! &&
	flux kvs get -c 1 --watch --waitcreate test.exec-hello.test-exec6 &&
	flux job wait-event -t 5 ${id5} debug.exec-reattach-finish &&
	test_debug "cat server6.log" &&
	grep "test-exec6: reattach: $(flux job id ${id5})" server6.log
'
test_expect_success NO_CHAIN_LINT 'exec hello: terminate disconnect-test job' '
	flux cancel ${id5} &&
	flux job wait-event -t 5 ${id5} clean &&
	kill ${SERVER6}
'

test_done
