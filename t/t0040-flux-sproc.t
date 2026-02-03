#!/bin/sh

test_description='Test flux-sproc command'

. $(dirname $0)/sharness.sh

test_under_flux 2 full -Slog-stderr-level=1

# Usage: wait_for_state RANK STATE COUNT
#
# wait up to 30s for number of procs in STATE (R or Z) on RANK to reach COUNT
#
wait_for_state() {
	rank=$1
	state=$2
	count=$3
	retries=0
	while test $(flux sproc ps -r $rank -no {state} \
		     | grep -c $state) -ne $count; do
		retries=$(($retries+1))
		test $retries -eq 300 && return 1 # max 300 * 0.1s sleep = 30s
		sleep 0.1
	done
}

test_expect_success 'flux sproc with no arguments fails' '
	test_must_fail flux sproc
'
test_expect_success 'flux sproc with invalid subcommand fails' '
	test_must_fail flux sproc invalid-subcommand
'
test_expect_success 'flux sproc ps works with no processes' '
	flux sproc ps -r1 >ps-empty.out &&
	test_debug "cat ps-empty.out" &&
	grep "PID" ps-empty.out &&
	test $(wc -l < ps-empty.out) -eq 1
'
test_expect_success 'flux sproc ps --no-header suppresses header' '
	flux sproc ps -n >ps-no-header.out &&
	test_debug "cat ps-no-header.out" &&
	test_must_fail grep "PID" ps-no-header.out
'
test_expect_success 'flux sproc ps shows background process' '
	flux exec -r 0 --bg --waitable --label=test-ps sleep inf &&
	flux sproc ps >ps-with-proc.out &&
	test_debug "cat ps-with-proc.out" &&
	grep test-ps ps-with-proc.out &&
	test_expect_code 143 flux sproc kill --wait 15 test-ps
'
test_expect_success 'flux sproc ps --format works' '
	flux exec -r 0 --bg --waitable --label=format-test sleep inf &&
	flux sproc ps -o "{pid} {label}" >ps-format.out &&
	test_debug "cat ps-format.out" &&
	grep "format-test" ps-format.out &&
	test_must_fail grep "COMMAND" ps-format.out &&
	test_expect_code 143 flux sproc kill --wait 15 format-test
'
test_expect_success 'flux sproc ps custom format with no header' '
	flux exec -r 0 --waitable --bg --label=custom sleep inf &&
	flux sproc ps -n -o "{label}:{state}" >ps-custom.out &&
	test_debug "cat ps-custom.out" &&
	grep "custom:R" ps-custom.out &&
	test_expect_code 143 flux sproc kill --wait 15 custom
'
test_expect_success 'flux sproc ps shows zombie state' '
	flux exec -r 0 --bg --waitable --label=zombie sleep 0 &&
	wait_for_state 0 Z 1 &&
	flux sproc ps >ps-zombie.out &&
	test_debug "cat ps-zombie.out" &&
	grep "Z.*zombie" ps-zombie.out &&
	flux sproc wait zombie
'
test_expect_success 'flux sproc ps --rank works' '
	flux exec -r 1 --bg --label=rank1 sleep inf &&
	flux sproc ps --rank 1 >ps-rank1.out &&
	test_debug "cat ps-rank1.out" &&
	grep rank1 ps-rank1.out &&
	flux sproc kill --rank 1 15 rank1
'
test_expect_success 'flux sproc ps with invalid rank fails' '
	test_must_fail flux sproc ps --rank 999
'
test_expect_success 'flux sproc kill by pid works' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r1 --bg --waitable sleep 30)
	EOF
	test_debug "echo launched pid=$pid on rank $rank" &&
	wait_for_state 1 R 1 &&
	flux sproc ps --rank=1 | grep $pid &&
	test_expect_code 143 flux sproc kill --wait -r 1 15 $pid &&
	flux sproc ps -r 1 >kill-bypid.out &&
	test_debug "cat kill-bypid.out" &&
	test_must_fail grep $pid kill-bypid.out
'
test_expect_success 'flux sproc kill by label works' '
	flux exec -r 0 --bg --waitable --label=kill-label sleep 30 &&
	flux sproc ps | grep kill-label &&
	test_expect_code 143 flux sproc kill --wait 15 kill-label
'
test_expect_success 'flux sproc kill with invalid pid fails' '
	test_must_fail flux sproc kill 15 999999 2>kill-badpid.err &&
	grep "does not belong" kill-badpid.err
'
test_expect_success 'flux sproc kill --wait with invalid pid fails' '
	test_must_fail flux sproc kill -w 15 999999 2>kill-wait-badpid.err &&
	test_debug "cat kill-wait-badpid.err" &&
	grep "wait failed" kill-wait-badpid.err
'
test_expect_success 'flux sproc kill with invalid label fails' '
	test_must_fail flux sproc kill 15 nonexistent-label 2>kill-badlabel.err &&
	grep "does not belong" kill-badlabel.err
'
test_expect_success 'flux sproc kill already exited process fails' '
	flux exec -r 1 --bg --waitable --label=already-exited sleep 0 &&
	wait_for_state 1 Z 1 &&
	test_expect_code 1 flux sproc kill -r1 15 already-exited \
	    2>already-exited.err &&
	test_debug "cat already-exited.err" &&
	grep "No such process" already-exited.err &&
	flux sproc wait -r1 already-exited
'
test_expect_success 'flux sproc kill --wait returns exit status' '
	flux exec -r 0 --bg --waitable --label=wait-kill sleep inf &&
	test_expect_code 143 flux sproc kill -w 15 wait-kill
'
test_expect_success 'flux sproc kill --wait waits even if kill failed' '
	flux exec -r 1 --bg --waitable --label=kill-fail sleep 0 &&
	wait_for_state 1 Z 1 &&
	flux sproc kill -wr 1 15 kill-fail
'
test_expect_success 'flux sproc wait by label works' '
	flux exec -r 0 --bg --waitable --label=wait-label sleep 0 &&
	flux sproc wait wait-label
'
test_expect_success 'flux sproc wait by pid works' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg --waitable sleep 0)
	EOF
	flux sproc wait $pid
'
test_expect_success 'flux sproc wait returns correct exit code' '
	flux exec -r 0 --bg --waitable --label=wait-exit false &&
	test_expect_code 1 flux sproc wait wait-exit
'
test_expect_success 'flux sproc wait returns signal exit code' '
	flux exec -r 0 --bg --waitable --label=wait-signal sleep inf &&
	flux sproc kill 9 wait-signal &&
	test_expect_code 137 flux sproc wait wait-signal
'
test_expect_success 'flux sproc wait on non-waitable process fails' '
	flux exec -r 0 --bg --label=not-waitable sleep inf &&
	test_must_fail flux sproc wait not-waitable 2>wait-notwaitable.err &&
	grep "not waitable" wait-notwaitable.err &&
	flux sproc kill 15 not-waitable
'
test_expect_success 'flux sproc wait on nonexistent process fails' '
	test_must_fail flux sproc wait 999999 2>wait-noexist.err &&
	grep "does not belong" wait-noexist.err
'
test_expect_success 'flux sproc wait on already-reaped process fails' '
	flux exec -r 0 --bg --waitable --label=already-reaped sleep 0 &&
	flux sproc wait already-reaped &&
	test_must_fail flux sproc wait already-reaped 2>wait-reaped.err
'
test_expect_success 'flux sproc with --service option works' '
	flux sproc ps --service rexec >service-rexec.out &&
	test_debug "cat service-rexec.out"
'
test_expect_success 'flux sproc help works' '
	flux sproc --help >help.out 2>&1 &&
	test_debug "cat help.out" &&
	grep "supported subcommands" help.out
'
test_expect_success 'flux sproc ps --help works' '
	flux sproc ps --help >ps-help.out 2>&1 &&
	test_debug "cat ps-help.out" &&
	grep "flux-sproc ps" ps-help.out
'
test_expect_success 'flux sproc kill --help works' '
	flux sproc kill --help >kill-help.out 2>&1 &&
	test_debug "cat kill-help.out" &&
	grep "flux-sproc kill" kill-help.out
'
test_expect_success 'flux sproc wait --help works' '
	flux sproc wait --help >wait-help.out 2>&1 &&
	test_debug "cat wait-help.out" &&
	grep "flux-sproc wait" wait-help.out
'
test_done
