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

test_under_flux 2 minimal -Slog-stderr-level=1

sdexec="flux exec --service sdexec"
wait="flux sproc wait --service sdexec"
kill="flux sproc kill --service sdexec"
ps="flux sproc ps --service sdexec"
groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

sleep=$(which sleep)

# The user systemd instance is shared with other tests running in parallel,
# but every unit name sdexec creates ends with this instance's suffix, derived
# from jobid-path, and its startup sweep matches only that suffix.  The test
# instance name is unique per flux-start, so no narrowing options are needed;
# a seeded orphan need only carry the suffix.  These brokers share a node, so
# the suffix includes the broker rank.  sdmon's system-bus monitoring
# is narrowed to a glob only this test could match (sys_glob), and the
# sdbus-sys bridge is pointed at the user bus so this works without
# privileged system bus access.
iname=$(flux getattr jobid-path | sed -e "s|^/||" -e "s|/|:|g")
orphan="t2419-orphan"
ounit="${orphan}-0:${iname}"
recov="t2419-recov"

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
	systemd-run --user --unit="${ounit}.service" \
	    --setenv=FLUX_SDEXEC_WAITABLE=1 \
	    --service-type=simple $sleep 300 &&
	systemctl --user is-active "${ounit}.service"
'
test_expect_success 'load sdbus-sys,sdbus,sdmon,sdexec modules' '
	flux module load --name sdbus-sys sdbus &&
	flux module load sdbus &&
	flux module load sdmon sys_glob="flux-t2419-$$-*" &&
	flux module load sdexec
'
# The startup sweep adopts the running orphan as a recovered proc, listed with
# its label and unit name (a recovered proc has no cmdline, so the command
# column falls back to the unit name).
test_expect_success 'sweep recovers the orphan into ps' '
	wait_for_ps_state "$orphan" R &&
	$ps -r 0 >orphan-ps.out &&
	test_debug "cat orphan-ps.out" &&
	grep "$orphan" orphan-ps.out &&
	$ps -r 0 -no "{cmd}" | grep "${ounit}.service"
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
	$sdexec -r 0 --bg --waitable --label="$recov" $sleep 300 &&
	wait_for_ps_state "$recov" R
'
test_expect_success 'reload sdexec to force a recovery sweep' '
	flux module remove sdexec &&
	flux module load sdexec
'
test_expect_success 'reloaded sdexec recovers the unit under its label' '
	wait_for_ps_state "$recov" R &&
	$ps -r 0 >recov-ps.out &&
	test_debug "cat recov-ps.out" &&
	grep "$recov" recov-ps.out &&
	$ps -r 0 -no "{cmd}" | grep "${recov}-0:${iname}.service"
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
# A unit that exits while sdexec is unloaded is preserved by RemainAfterExit
# in the active/exited state; the next sweep adopts it and a later wait
# collects its status.
test_expect_success 'unit that exits while sdexec is unloaded is waitable' '
	$sdexec -r 0 --bg --waitable --label=t2419-exited \
	    /bin/sh -c "sleep 2" &&
	wait_for_ps_state t2419-exited R &&
	flux module remove sdexec &&
	test_wait_until "systemctl --user show -p SubState \
	    t2419-exited-0:${iname}.service | grep -q =exited" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-exited Z &&
	$wait -r 0 t2419-exited
'

# A unit that exits NONZERO while sdexec is unloaded lands in the systemd
# failed state, which retains the exit status until the unit is reset.  The
# sweep adopts it, the state machine resets the failed unit, and a later
# wait collects the status.
test_expect_success 'unit that fails while sdexec is unloaded is waitable' '
	$sdexec -r 0 --bg --waitable --label=t2419-failed \
	    /bin/sh -c "sleep 2 && exit 7" &&
	wait_for_ps_state t2419-failed R &&
	flux module remove sdexec &&
	test_wait_until "systemctl --user show -p ActiveState \
	    t2419-failed-0:${iname}.service | grep -q =failed" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-failed Z &&
	test_expect_code 7 $wait -r 0 t2419-failed &&
	test_wait_until "systemctl --user show -p LoadState \
	    t2419-failed-0:${iname}.service | grep -q =not-found"
'

# A finished waitable unit is held loaded in systemd as the durable copy of
# its exit status: each reload re-adopts it, and only the wait reaps it.
test_expect_success 'finished unit is held until waited across reloads' '
	$sdexec -r 0 --bg --waitable --label=t2419-hold \
	    /bin/sh -c "exit 5" &&
	wait_for_ps_state t2419-hold Z &&
	flux module reload sdexec &&
	wait_for_ps_state t2419-hold Z &&
	flux module reload sdexec &&
	wait_for_ps_state t2419-hold Z &&
	$ps -r 0 -no "{label} {flags} {attached}" >hold-fields.out &&
	grep "^t2419-hold bg,waitable -" hold-fields.out &&
	test_expect_code 5 $wait -r 0 t2419-hold &&
	test_wait_until "systemctl --user show -p LoadState \
	    t2419-hold-0:${iname}.service | grep -q =not-found"
'

# A leftover unit without the waitable marker has no reclaiming client, so
# the sweep downgrades it and reaps it at adoption instead of holding it.
test_expect_success 'unmarked finished leftover is reaped by the sweep' '
	systemd-run --user --unit="t2419-unmarked-0:${iname}.service" \
	    --remain-after-exit --service-type=simple /bin/true &&
	test_wait_until "systemctl --user show -p SubState \
	    t2419-unmarked-0:${iname}.service | grep -q =exited" &&
	flux module reload sdexec &&
	test_wait_until "systemctl --user show -p LoadState \
	    t2419-unmarked-0:${iname}.service | grep -q =not-found" &&
	$ps -r 0 >unmarked-ps.out &&
	test_must_fail grep t2419-unmarked unmarked-ps.out
'

# Both brokers share this node's systemd user instance, so each sdexec
# appends its rank to unit names and sweeps only units carrying its own
# rank.  Start a unit on each rank, reload both sdexecs, and confirm each
# recovered only its own: a rank-blind sweep would cross-adopt.
test_expect_success 'load sdbus,sdexec on rank 1' '
	flux exec -r 1 flux module load sdbus &&
	flux exec -r 1 flux module load sdexec
'
test_expect_success 'start one unit per rank and reload both sdexecs' '
	$sdexec -r 0 --bg --waitable --label=t2419-r0 $sleep 300 &&
	$sdexec -r 1 --bg --waitable --label=t2419-r1 $sleep 300 &&
	wait_for_ps_state t2419-r0 R &&
	flux module reload sdexec &&
	flux exec -r 1 flux module reload sdexec
'
test_expect_success 'each rank recovered its own unit and no others' '
	wait_for_ps_state t2419-r0 R &&
	$ps -r 0 -no "{label}" >rank0.labels &&
	$ps -r 1 -no "{label}" >rank1.labels &&
	test_debug "cat rank0.labels rank1.labels" &&
	grep -x t2419-r0 rank0.labels &&
	grep -x t2419-r1 rank1.labels &&
	test_must_fail grep -x t2419-r1 rank0.labels &&
	test_must_fail grep -x t2419-r0 rank1.labels
'
test_expect_success 'reap the per-rank units' '
	$kill -r 0 15 t2419-r0 &&
	test_expect_code 143 $wait -r 0 t2419-r0 &&
	$kill -r 1 15 t2419-r1 &&
	test_expect_code 143 $wait -r 1 t2419-r1
'
test_expect_success 'remove rank 1 modules' '
	flux exec -r 1 flux module remove sdexec &&
	flux exec -r 1 flux module remove sdbus
'

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
	systemctl --user stop "t2419-*" 2>/dev/null || true &&
	systemctl --user reset-failed "t2419-*" 2>/dev/null || true
'
test_done
