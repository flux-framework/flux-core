#!/bin/sh
# ci=system

test_description='Test sdexec background execution and wait'

. $(dirname $0)/sharness.sh

if ! flux version | grep systemd; then
	skip_all="flux was not built with systemd"
	test_done
fi
if ! systemctl --user show --property Version; then
	skip_all="user systemd is not running"
	test_done
fi
if ! busctl --user status >/dev/null; then
	skip_all="user dbus is not running"
	test_done
fi
if ! test_flux_security_version 0.14.0; then
	skip_all="requires flux-security >= v0.14, got ${FLUX_SECURITY_VERSION}"
	test_done
fi

test_under_flux 2 minimal -Slog-stderr-level=1

sdexec="flux exec --service sdexec"
wait="flux sproc wait --service sdexec"
kill="flux sproc kill --service sdexec"
ps="flux sproc ps --service sdexec"

# systemd 239 requires commands to be fully qualified, while 249 does not
true=$(which true)
false=$(which false)
sh=$(which sh)
sleep=$(which sleep)

test_expect_success 'enable debug logging' '
	cat >systemd.toml <<-EOF &&
	[systemd]
	sdbus-debug = true
	sdexec-debug = true
	EOF
	flux config load <systemd.toml
'
test_expect_success 'load sdbus,sdexec modules' '
	flux exec flux module load sdbus &&
	flux exec flux module load sdexec
'
test_expect_success 'background exec of true succeeds' '
	$sdexec -r 0 --bg $true
'
test_expect_success 'background exec prints rank and pid' '
	$sdexec -r 0 --bg $true >bg.out &&
	grep "^0: [0-9][0-9]*$" bg.out
'
test_expect_success 'waitable requires --bg' '
	test_must_fail $sdexec -r 0 --waitable $true 2>waitable.err &&
	grep "waitable can only be used with --bg" waitable.err
'
test_expect_success 'wait on waitable process returns exit 0' '
	$sdexec -r 0 --bg --waitable --label=wait-true $true &&
	$wait wait-true
'
test_expect_success 'wait on waitable process returns nonzero exit code' '
	$sdexec -r 0 --bg --waitable --label=wait-false $false &&
	test_expect_code 1 $wait wait-false
'
test_expect_success 'wait returns arbitrary exit code' '
	$sdexec -r 0 --bg --waitable --label=wait-199 $sh -c "exit 199" &&
	test_expect_code 199 $wait wait-199
'
test_expect_success 'wait by pid works' '
	IFS=": " read -r rank pid <<-EOF &&
	$($sdexec -r 0 --bg --waitable $true)
	EOF
	test_debug "echo waiting for pid=$pid on rank=$rank" &&
	$wait -r 0 $pid
'
test_expect_success 'wait parks until a running process exits' '
	$sdexec -r 0 --bg --waitable --label=wait-sleep $sh -c "sleep 1" &&
	$wait wait-sleep
'
test_expect_success 'wait returns signal exit code' '
	$sdexec -r 0 --bg --waitable --label=wait-signal $sleep 30 &&
	$kill -r 0 9 wait-signal &&
	test_expect_code 137 $wait wait-signal
'
test_expect_success 'kill by pid works' '
	IFS=": " read -r rank pid <<-EOF &&
	$($sdexec -r 0 --bg --waitable $sleep 30)
	EOF
	test_expect_code 143 $kill -r 0 -w 15 $pid
'
test_expect_success 'kill on nonexistent pid fails' '
	test_must_fail $kill -r 0 15 999999 2>killnoexist.err &&
	grep -i "not found" killnoexist.err
'
test_expect_success 'wait --output returns retained stdout' '
	$sdexec -r 0 --bg --waitable --label=wait-out \
	    $sh -c "echo hello from bg" &&
	$wait --output wait-out >waitout.out &&
	grep "hello from bg" waitout.out
'
test_expect_success 'wait --output returns retained stderr' '
	$sdexec -r 0 --bg --waitable --label=wait-err \
	    $sh -c "echo oops >&2" &&
	$wait --output wait-err 2>waiterr.err &&
	grep "oops" waiterr.err
'
test_expect_success 'wait on non-waitable background process fails' '
	$sdexec -r 0 --bg --label=not-waitable $sleep 30 &&
	test_must_fail $wait not-waitable 2>notwaitable.err &&
	test_debug "cat notwaitable.err" &&
	grep -i "not waitable" notwaitable.err &&
	$kill -r 0 15 not-waitable
'
test_expect_success 'wait on nonexistent process fails' '
	test_must_fail $wait 999999 2>noexist.err &&
	grep -i "not found" noexist.err
'
# The $wait CLI blocks until the process exits, so it cannot express "park a
# wait, then probe" without a race.  This helper drives Flux handles directly:
# a synchronous round-trip (barrier) exploits in-order per-client request
# processing to know when a parked wait has been registered by the module.
test_expect_success 'create bg wait helper script' '
	cat >bgwait.py <<-'"'"'EOT'"'"' &&
	import os
	import sys
	import flux
	import flux.subprocess as sp
	SERVICE = "sdexec"
	RANK = 0
	def barrier(h):
	    # A round-trip on handle h.  The module handles a single client in FIFO
	    # order, so any request sent on h beforehand has been acted upon by the
	    # time this returns.
	    sp.list(h, service=SERVICE, nodeid=RANK).get()
	def park_and_exit(label):
	    # Park a wait on a running process, then exit.  Interpreter shutdown
	    # closes the handle, which the broker delivers to sdexec as a disconnect.
	    h = flux.Flux()
	    sp.wait(h, label=label, service=SERVICE, nodeid=RANK)
	    barrier(h)
	def rewait_kill(label):
	    # Attach a new waiter after the previous one disconnected, retrying while
	    # the module still reports the process as being waited on (until it has
	    # processed that disconnect), then kill and collect the status.
	    h = flux.Flux()
	    while True:
	        rpc = sp.wait(h, label=label, service=SERVICE, nodeid=RANK)
	        barrier(h)
	        if not rpc.is_ready():
	            break  # the wait is parked: this client now owns it
	        try:
	            rpc.get_status()
	            sys.exit("wait completed unexpectedly")
	        except OSError:
	            pass  # already being waited on: disconnect not processed yet
	    sp.kill(h, signum=15, label=label, service=SERVICE, nodeid=RANK).get()
	    status = rpc.get_status()
	    if not (os.WIFSIGNALED(status) and os.WTERMSIG(status) == 15):
	        sys.exit(f"unexpected wait status {status}")
	def double_wait(label):
	    # Only one outstanding waiter is allowed, so a second wait must fail.
	    h = flux.Flux()
	    rpc1 = sp.wait(h, label=label, service=SERVICE, nodeid=RANK)
	    barrier(h)
	    try:
	        sp.wait(h, label=label, service=SERVICE, nodeid=RANK).get_status()
	        sys.exit("second wait unexpectedly succeeded")
	    except OSError as exc:
	        print(exc)
	    return rpc1
	{"park_and_exit": park_and_exit,
	 "rewait_kill": rewait_kill,
	 "double_wait": double_wait}[sys.argv[1]](sys.argv[2])
	EOT
	chmod +x bgwait.py
'
test_expect_success 'wait fails when process is already being waited on' '
	$sdexec -r 0 --bg --waitable --label=wait-busy $sleep 30 &&
	flux python bgwait.py double_wait wait-busy >busy.out 2>&1 &&
	test_debug "cat busy.out" &&
	grep -i "already being waited on" busy.out &&
	$kill -r 0 15 wait-busy &&
	test_expect_code 143 $wait wait-busy
'
test_expect_success 'new client can wait after previous waiter disconnects' '
	$sdexec -r 0 --bg --waitable --label=wait-reconnect $sleep 30 &&
	flux python bgwait.py park_and_exit wait-reconnect &&
	flux python bgwait.py rewait_kill wait-reconnect
'
# Usage: wait_for_ps_state LABEL STATE
# wait up to 30s for the process with the given label to reach STATE (R or Z)
wait_for_ps_state() {
	retries=0
	while ! $ps -r 0 -no "{state} {label}" | grep -q "^$2 $1$"; do
		retries=$(($retries+1))
		test $retries -eq 300 && return 1 # max 300 * 0.1s = 30s
		sleep 0.1
	done
}
test_expect_success 'ps reports label and R state for a running process' '
	$sdexec -r 0 --bg --waitable --label=ps-running $sleep 30 &&
	wait_for_ps_state ps-running R &&
	$ps -r 0 >ps-running.out &&
	test_debug "cat ps-running.out" &&
	grep ps-running ps-running.out &&
	$kill -r 0 15 ps-running &&
	test_expect_code 143 $wait ps-running
'
test_expect_success 'ps reports Z state for a finished waitable process' '
	$sdexec -r 0 --bg --waitable --label=ps-zombie $true &&
	wait_for_ps_state ps-zombie Z &&
	$wait ps-zombie
'
# A non-waitable background process is detached, so there is nothing to wait
# on; poll the broker log until its output appears (or time out).
grep_dmesg_retry() {
	retries=0
	while ! flux dmesg | grep -q "$1"; do
		retries=$(($retries+1))
		test $retries -eq 300 && return 1 # max 300 * 0.1s = 30s
		sleep 0.1
	done
}
test_expect_success 'non-waitable background output is logged to broker log' '
	flux dmesg -C &&
	$sdexec -r 0 --bg $sh -c "echo detached-output" &&
	grep_dmesg_retry detached-output
'
test_expect_success 'remove sdexec,sdbus modules' '
	flux exec flux module remove sdexec &&
	flux exec flux module remove sdbus
'
test_done
