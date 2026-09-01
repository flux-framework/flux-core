#!/bin/sh
# ci=system

test_description='Test sdexec recovery of leftover units across a restart'

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
if ! command -v systemd-run >/dev/null; then
	skip_all="systemd-run is not available"
	test_done
fi
if ! test_flux_security_version 0.14.0; then
	skip_all="requires flux-security >= v0.14, got ${FLUX_SECURITY_VERSION}"
	test_done
fi

test_under_flux 1 minimal -Slog-stderr-level=1

sdexec="flux exec --service sdexec"
wait="flux sproc wait --service sdexec"
kill="flux sproc kill --service sdexec"
ps="flux sproc ps --service sdexec"
groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

sleep=$(which sleep)

# The user systemd instance is shared with other tests running in parallel, so
# unit names carry a unique per-run prefix, sdexec's startup sweep is narrowed
# to that prefix (recover_glob), and sdmon's system-bus monitoring is narrowed
# to a glob only this test could match (sys_glob).  The sdbus-sys bridge is
# pointed at the user bus so this works without privileged system bus access.
# bulk-exec forms job-shell unit names as shell-<rank>-<jobid>; the sweep
# allow-list requires the shell- prefix.
prefix="shell-0-t2419-$$"
orphan="${prefix}-orphan"
recov="${prefix}-recov"

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

test_expect_success 'enable debug logging' '
	cat >systemd.toml <<-EOF &&
	[systemd]
	sdbus-debug = true
	sdexec-debug = true
	EOF
	flux config load <systemd.toml
'
# Seed a running shell-named user unit before sdexec ever loads.  It is not
# reaped until the end of the test, so sdexec's clean decision stays withheld
# and the sdmon.online group is never joined until then.  systemd-run (rather
# than sdexec) starts it so it is already present at sdexec's first sweep.
test_expect_success 'seed a stray orphan unit before loading sdexec' '
	systemd-run --user --unit="${orphan}.service" \
	    --service-type=simple $sleep 300 &&
	systemctl --user is-active "${orphan}.service"
'
test_expect_success 'load sdbus-sys,sdbus,sdmon,sdexec modules' '
	flux module load --name sdbus-sys sdbus &&
	flux module load sdbus &&
	flux module load sdmon sys_glob="flux-t2419-$$-*" &&
	flux module load sdexec recover_glob="${prefix}-*"
'
# The startup sweep adopts the running orphan as a recovered proc, listed with
# its label and unit name (a recovered proc has no cmdline, so the command
# column falls back to the unit name).
test_expect_success 'sweep recovers the orphan into ps' '
	wait_for_ps_state "$orphan" R &&
	$ps -r 0 >orphan-ps.out &&
	test_debug "cat orphan-ps.out" &&
	grep "$orphan" orphan-ps.out &&
	$ps -r 0 -no "{cmd}" | grep "${orphan}.service"
'
# An un-reclaimed running orphan holds the user bus dirty, so sdexec never
# signals sdmon and the node stays out of the online group (an empty member
# list).
test_expect_success 'orphan holds the node offline' '
	test -z "$($groups get sdmon.online)"
'
# Recovery proper: start a unit through sdexec, drop it by reloading sdexec,
# and confirm the sweep re-adopts it under its label.  A --label matching the
# unit basename lets us wait on it by label both before and after the reload
# (a live proc carries the label only if one was set).
test_expect_success 'start a background waitable unit through sdexec' '
	$sdexec -r 0 --bg --waitable --label="$recov" \
	    --setopt SDEXEC_NAME="${recov}.service" $sleep 300 &&
	wait_for_ps_state "$recov" R
'
test_expect_success 'reload sdexec to force a recovery sweep' '
	flux module remove sdexec &&
	flux module load sdexec recover_glob="${prefix}-*"
'
test_expect_success 'reloaded sdexec recovers the unit under its label' '
	wait_for_ps_state "$recov" R &&
	$ps -r 0 >recov-ps.out &&
	test_debug "cat recov-ps.out" &&
	grep "$recov" recov-ps.out &&
	$ps -r 0 -no "{cmd}" | grep "${recov}.service"
'
# The recovered unit is still running, so a wait parks until it is reaped.  Its
# stdio channels died with the old module, so no output can be retained: a
# --output wait returns the exit status only (SIGTERM => 143) and empty output.
test_expect_success 'wait by label recovers status only, no output' '
	$kill -r 0 15 "$recov" &&
	test_expect_code 143 $wait --output "$recov" >recov-wait.out &&
	test_debug "cat recov-wait.out" &&
	test ! -s recov-wait.out
'
# Reaping the recovered unit does not release the node: the seed orphan is
# still running and still un-reclaimed.
test_expect_success 'node still offline while the orphan runs' '
	test -z "$($groups get sdmon.online)"
'
# Reap the last orphan (it too can be waited by label); once no orphan blocks
# the clean decision, sdexec signals sdmon and the node joins online.
test_expect_success 'reaping the orphan brings the node online' '
	$kill -r 0 15 "$orphan" &&
	test_expect_code 143 $wait "$orphan" &&
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'remove modules' '
	flux module remove sdexec &&
	flux module remove sdmon &&
	flux module remove sdbus &&
	flux module remove sdbus-sys
'
test_expect_success 'clean up any residual test units' '
	systemctl --user stop "${prefix}-*" 2>/dev/null || true &&
	systemctl --user reset-failed "${prefix}-*" 2>/dev/null || true
'
test_done
