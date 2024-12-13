#!/bin/sh

test_description='Test streaming flux job statistics RPC'

. $(dirname $0)/sharness.sh

test_under_flux 1

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

get_watchers() {
	flux module stats job-list | jq -r .stats_watchers
}

test_expect_success 'create streaming job-stats script' '
	cat >job-stats.py <<-EOT &&
	import sys
	import flux
	handle = flux.Flux()
	fut = handle.rpc("job-list.job-stats",{},flags=flux.constants.FLUX_RPC_STREAMING)
	for i in range(int(sys.argv[1])):
	    print(fut.get())
	    sys.stdout.flush()
	    fut.reset()
	EOT
	chmod +x job-stats.py
'
test_expect_success 'streaming job-stats returns one response immediately' '
	run_timeout 30 flux python job-stats.py 1
'
test_expect_success 'disconnect handling works' '
	test $(get_watchers) -eq 0
'
test_expect_success NO_CHAIN_LINT 'start monitoring job-stats and wait for first response' '
	flux python job-stats.py 100 >stats.log &
	echo $! >stats.pid &&
	$waitfile stats.log
'
test_expect_success NO_CHAIN_LINT 'run a job to completion' '
	flux run true
'
test_expect_success NO_CHAIN_LINT 'wait until job-stats produces at least one more response' '
	$waitfile --count=2 stats.log
'
test_expect_success NO_CHAIN_LINT 'kill job-stats script' '
	pid=$(cat stats.pid) &&
	kill -15 $pid
'

test_done
