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
if ! command -v systemd-notify >/dev/null; then
	skip_all="systemd-notify is not available"
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

sleep=$(which sleep)

# The user systemd instance is shared with other tests running in parallel,
# but every unit name sdexec creates ends with this instance's suffix, derived
# from jobid-path, and its startup sweep matches only that suffix.  The test
# instance name is unique per flux-start, so no narrowing options are needed;
# a seeded orphan need only carry the suffix.  These brokers share a node, so
# the suffix includes the broker rank, and a unit name is
# "<label>:<rank>-<instance>.service".
iname=$(flux getattr jobid-path | sed -e "s|^/||" -e "s|/|-|g")
orphan="t2419-orphan"
ounit="${orphan}:0-${iname}"
recov="t2419-recov"

# Unit records live in a test-private directory rather than the shared
# XDG_RUNTIME_DIR so a failed run leaves nothing behind for other tests.
recdir=$(pwd)/records

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
# Usage: unit_prop NAME PROPERTY
unit_prop() {
	systemctl --user show -p $2 --value "$1" 2>/dev/null
}
# Usage: record NAME  (the unit record path)
record() {
	echo "$recdir/$1:0-${iname}.service"
}
# Usage: start_context NAME  (the context of the record's start event)
start_context() {
	head -n1 $(record $1) | jq -e "select(.name == \"start\") | .context"
}
# Usage: has_exit NAME  (true if systemd appended an exit event)
has_exit() {
	grep -q "\"name\":\"exit\"" $(record $1)
}

test_expect_success 'configure debug logging and a record directory' '
	mkdir -p $recdir &&
	cat >systemd.toml <<-EOF &&
	[systemd]
	sdbus-debug = true
	sdexec-debug = true
	sdexec-record-dir = "$recdir"
	EOF
	flux config load <systemd.toml
'
# Seed a running unit carrying this instance's suffix before sdexec ever
# loads, so it is already present at sdexec's first sweep.  systemd-run
# (rather than sdexec) starts it so there is no record of it.
test_expect_success 'seed a stray orphan unit before loading sdexec' '
	systemd-run --user --unit="${ounit}.service" \
	    --service-type=simple $sleep 300 &&
	systemctl --user is-active "${ounit}.service"
'
test_expect_success 'load sdbus,sdexec modules' '
	flux module load sdbus &&
	flux module load sdexec
'
# A recordless orphan is adopted as bg,waitable with its label derived
# from the unit name; with no record there is no cmdline so the unit name
# stands in for the command.
test_expect_success 'sweep recovers the orphan into ps' '
	wait_for_ps_state "$orphan" R &&
	$ps -r 0 -no "{label} {flags} {cmd}" >orphan-ps.out &&
	test_debug "cat orphan-ps.out" &&
	grep "^$orphan bg,waitable ${ounit}.service$" orphan-ps.out
'
# Every unit sdexec starts gets a record before it is started.
test_expect_success 'a unit started through sdexec has a record' '
	$sdexec -r 0 --bg --waitable --label="$recov" $sleep 300 &&
	wait_for_ps_state "$recov" R &&
	test -f $(record $recov) &&
	start_context $recov >recov-start.json &&
	test_debug "cat recov-start.json" &&
	jq -e ".label == \"$recov\" and .waitable == true and .bg == true" \
	    recov-start.json &&
	jq -e ".cmdline == [\"$sleep\", \"300\"]" recov-start.json
'
# Recovery proper: drop the unit by reloading sdexec, and confirm the sweep
# re-adopts it under its label with its command line from the record.
test_expect_success 'reload sdexec to force a recovery sweep' '
	flux module remove sdexec &&
	flux module load sdexec
'
test_expect_success 'reloaded sdexec recovers the unit from its record' '
	wait_for_ps_state "$recov" R &&
	$ps -r 0 -no "{label} {flags} {cmd}" >recov-ps.out &&
	test_debug "cat recov-ps.out" &&
	grep "^$recov bg,waitable $sleep$" recov-ps.out
'
# The recovered unit is still running, so a wait parks until it is reaped.
# Its stdio channels died with the old module, so no output can be retained:
# a --output wait returns the exit status only (SIGTERM => 143).  The record
# is removed once the status has been collected.
test_expect_success 'wait by label recovers status only, no output' '
	$kill -r 0 15 "$recov" &&
	test_expect_code 143 $wait --output "$recov" >recov-wait.out &&
	test_debug "cat recov-wait.out" &&
	test ! -s recov-wait.out &&
	test ! -f $(record $recov)
'
# A unit that exits while sdexec is unloaded has its status recorded by
# systemd (ExecStopPost) as an exit event in its record.  The next sweep adopts it as a
# finished process and a later wait collects the status.
test_expect_success 'unit that exits while sdexec is unloaded is waitable' '
	$sdexec -r 0 --bg --waitable --label=t2419-exited \
	    /bin/sh -c "sleep 2" &&
	wait_for_ps_state t2419-exited R &&
	flux module remove sdexec &&
	test_wait_until "test \"\$(unit_prop t2419-exited:0-${iname}.service \
	    SubState)\" = exited" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-exited Z &&
	$wait -r 0 t2419-exited &&
	test_wait_until "test \"\$(unit_prop t2419-exited:0-${iname}.service \
	    LoadState)\" = not-found" &&
	test ! -f $(record t2419-exited)
'
test_expect_success 'unit that fails while sdexec is unloaded is waitable' '
	$sdexec -r 0 --bg --waitable --label=t2419-failed \
	    /bin/sh -c "sleep 2 && exit 7" &&
	wait_for_ps_state t2419-failed R &&
	flux module remove sdexec &&
	test_wait_until "test \"\$(unit_prop t2419-failed:0-${iname}.service \
	    ActiveState)\" = failed" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-failed Z &&
	test_expect_code 7 $wait -r 0 t2419-failed &&
	test_wait_until "test \"\$(unit_prop t2419-failed:0-${iname}.service \
	    LoadState)\" = not-found" &&
	test ! -f $(record t2419-failed)
'
# The production path: job-exec runs IMP units with Type=notify, and the IMP
# sends STOPPING=1 before exiting.  A unit that then exits 0 goes straight to
# inactive/dead and systemd garbage collects it, so there is no unit left to
# adopt.  The exit event systemd wrote is the only trace of the status, and
# the sweep recovers the process from it.
test_expect_success 'notify unit exiting 0 after STOPPING=1 is collected' '
	$sdexec -r 0 --bg --waitable --label=t2419-notify0 \
	    --setopt=SDEXEC_PROP_Type=notify \
	    --setopt=SDEXEC_PROP_NotifyAccess=all \
	    /bin/sh -c "systemd-notify --ready; sleep 2; \
	        systemd-notify STOPPING=1; exit 0" &&
	wait_for_ps_state t2419-notify0 R &&
	flux module remove sdexec &&
	test_wait_until "test \"\$(unit_prop t2419-notify0:0-${iname}.service \
	    LoadState)\" = not-found" &&
	cat $(record t2419-notify0) &&
	has_exit t2419-notify0 &&
	jq -se "length == 2 and .[0].name == \"start\" and .[1].name == \"exit\"
	    and .[1].timestamp >= .[0].timestamp" $(record t2419-notify0) &&
	tail -n1 $(record t2419-notify0) \
	    | jq -e ".context == {\"result\":\"success\",\"code\":\"exited\",\"status\":\"0\"}" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-notify0 Z &&
	$wait -r 0 t2419-notify0 &&
	test ! -f $(record t2419-notify0)
'
test_expect_success 'notify unit exiting 3 after STOPPING=1 is collected' '
	$sdexec -r 0 --bg --waitable --label=t2419-notify3 \
	    --setopt=SDEXEC_PROP_Type=notify \
	    --setopt=SDEXEC_PROP_NotifyAccess=all \
	    /bin/sh -c "systemd-notify --ready; sleep 2; \
	        systemd-notify STOPPING=1; exit 3" &&
	wait_for_ps_state t2419-notify3 R &&
	flux module remove sdexec &&
	test_wait_until "has_exit t2419-notify3" &&
	flux module load sdexec &&
	wait_for_ps_state t2419-notify3 Z &&
	test_expect_code 3 $wait -r 0 t2419-notify3 &&
	test ! -f $(record t2419-notify3)
'
# A process started without a label is given a generated one, which is
# kept in the record so that it is the same after a restart.  If it exits
# while sdexec is unloaded and systemd collects the unit, recovery is logged
# in one line naming the process by its command (no pid is known), and it
# can be waited on by the generated label.
test_expect_success 'unlabeled unit that exits while unloaded is waitable' '
	$sdexec -r 0 --bg --waitable \
	    --setopt=SDEXEC_PROP_Type=notify \
	    --setopt=SDEXEC_PROP_NotifyAccess=all \
	    /bin/sh -c "systemd-notify --ready; sleep 2; \
	        systemd-notify STOPPING=1; exit 0" &&
	$ps -r 0 -no "{label} {cmd}" >nolabel-ps.out &&
	test_debug "cat nolabel-ps.out" &&
	label=$(awk "\$2 == \"/bin/sh\" {print \$1}" nolabel-ps.out) &&
	test -n "$label" &&
	test "$label" != "-" &&
	start_context $label | jq -e ".label_generated == true" &&
	flux module remove sdexec &&
	test_wait_until "test \"\$(unit_prop $label:0-${iname}.service \
	    LoadState)\" = not-found" &&
	flux dmesg -C &&
	flux module load sdexec &&
	$ps -r 0 -no "{state} {label}" >nolabel-ps2.out &&
	test_debug "cat nolabel-ps2.out" &&
	grep "^Z $label$" nolabel-ps2.out &&
	$wait -r 0 $label &&
	test ! -f $(record $label) &&
	flux dmesg >nolabel-dmesg.out &&
	test_debug "cat nolabel-dmesg.out" &&
	grep "recover $label:0-${iname}.service: sh: Exit 0 (held for wait)" \
	    nolabel-dmesg.out &&
	test_must_fail grep "unknown.unknown" nolabel-dmesg.out
'
# A finished waitable process survives any number of reloads until waited:
# the first reload reaps the unit, and the record carries the status after
# that.
test_expect_success 'finished unit is retained across reloads until waited' '
	$sdexec -r 0 --bg --waitable --label=t2419-hold /bin/sh -c "exit 5" &&
	wait_for_ps_state t2419-hold Z &&
	flux module reload sdexec &&
	wait_for_ps_state t2419-hold Z &&
	flux module reload sdexec &&
	wait_for_ps_state t2419-hold Z &&
	$ps -r 0 -no "{label} {flags} {attached}" >hold-fields.out &&
	grep "^t2419-hold bg,waitable False" hold-fields.out &&
	test_expect_code 5 $wait -r 0 t2419-hold &&
	test ! -f $(record t2419-hold)
'
# A record whose unit is gone and that has no exit status means the unit
# died without running its stop path (here, the exit event is deleted to
# simulate the user systemd instance being killed).  The process is lost:
# the wait fails with ESRCH rather than reporting a clean exit.
test_expect_success 'lost unit (record, no exit, no unit) fails wait' '
	$sdexec -r 0 --bg --waitable --label=t2419-lost /bin/true &&
	wait_for_ps_state t2419-lost Z &&
	flux module remove sdexec &&
	sed -i "/\"name\":\"exit\"/d" $(record t2419-lost) &&
	! has_exit t2419-lost &&
	flux module load sdexec &&
	wait_for_ps_state t2419-lost Z &&
	test_must_fail $wait -r 0 t2419-lost 2>lost.err &&
	grep -i "no such process\|vanished" lost.err &&
	test ! -f $(record t2419-lost)
'
# An exit event cut short (ExecStopPost killed mid-write) is unreadable.
# The status is unknown, so the process is reported lost, never as a
# clean exit, and the log says why.
test_expect_success 'unit with a truncated exit event fails wait' '
	$sdexec -r 0 --bg --waitable --label=t2419-trunc /bin/true &&
	wait_for_ps_state t2419-trunc Z &&
	flux module remove sdexec &&
	sed -i "s/\"name\":\"exit\".*/\"name\":\"ex/" \
	    $(record t2419-trunc) &&
	printf %s "$(cat $(record t2419-trunc))" >$(record t2419-trunc) &&
	flux module load sdexec &&
	wait_for_ps_state t2419-trunc Z &&
	test_must_fail $wait -r 0 t2419-trunc 2>trunc.err &&
	test_debug "cat trunc.err" &&
	grep "unreadable" trunc.err &&
	flux dmesg | grep "t2419-trunc.*lost.*unreadable" &&
	test ! -f $(record t2419-trunc)
'
# A clean unload kills and reaps every non-waitable unit, so only a crash
# that skips teardown can leave a non-waitable record behind.  Simulate one
# by seeding a record with an exit event for a unit that no longer exists:
# nobody will collect its status, so the sweep logs the exit and removes it.
test_expect_success 'non-waitable exited record is reaped by the sweep' '
	flux module remove sdexec &&
	cat >$(record t2419-nw) <<-EOT &&
	{"timestamp":1.0,"name":"start","context":{"label":"t2419-nw","bg":true,"waitable":false}}
	{"timestamp":2.0,"name":"exit","context":{"result":"exit-code","code":"exited","status":"1"}}
	EOT
	flux module load sdexec &&
	$ps -r 0 -no "{label}" >nw.labels &&
	test_must_fail grep -x t2419-nw nw.labels &&
	test ! -f $(record t2419-nw)
'
# A crash between writing a record and renaming it into place leaves a
# temporary file.  The sweep removes those for its own units only.  A list
# request is held until the sweep is done, so it marks the sweep's end.
test_expect_success 'sweep removes leftover temporary record files' '
	flux module remove sdexec &&
	touch $(record t2419-tmp).tmp &&
	touch $recdir/t2419-tmp:0-other.service.tmp &&
	flux module load sdexec &&
	$ps -r 0 >/dev/null &&
	test ! -f $(record t2419-tmp).tmp &&
	test -f $recdir/t2419-tmp:0-other.service.tmp &&
	rm -f $recdir/t2419-tmp:0-other.service.tmp
'
# Recovery restores the stop timer settings from the record.  Arm a short
# stop timer with SIGTERM as the escalation signal on a notify unit that
# sends STOPPING=1 and then lingers (as an IMP does with stragglers in its
# cgroup): the recovered proc's timer must fire and kill it.
test_expect_success 'recovered unit keeps its stop timer settings' '
	$sdexec -r 0 --bg --waitable --label=t2419-timer \
	    --setopt=SDEXEC_PROP_Type=notify \
	    --setopt=SDEXEC_PROP_NotifyAccess=all \
	    --setopt=SDEXEC_STOP_TIMER_SEC=2 \
	    --setopt=SDEXEC_STOP_TIMER_SIGNAL=15 \
	    /bin/sh -c "systemd-notify --ready; sleep 1; \
	        systemd-notify STOPPING=1; sleep 300" &&
	wait_for_ps_state t2419-timer R &&
	start_context t2419-timer \
	    | jq -e ".stop_timer_sec == 2 and .stop_timer_signal == 15" &&
	flux module remove sdexec &&
	test_wait_until "test \"\$(unit_prop t2419-timer:0-${iname}.service \
	    ActiveState)\" = deactivating" &&
	flux module load sdexec &&
	test_expect_code 143 $wait -r 0 t2419-timer
'
# Requests are held until the startup sweep completes, so a wait issued
# immediately after load must find a leftover without any polling.
test_expect_success 'wait issued immediately after load finds leftover' '
	$sdexec -r 0 --bg --waitable --label=t2419-sync /bin/true &&
	wait_for_ps_state t2419-sync Z &&
	flux module reload sdexec &&
	$wait -r 0 t2419-sync
'
# The sweep waits as long as sdbus takes to answer, holding requests in the
# meantime rather than failing them or the module load.  Force sdbus into its
# reconnect backoff (a 2s delay before it reconnects), load sdexec during it,
# and confirm that a wait and a list sent before the sweep can finish are
# held and then answered from the recovered process table.
test_expect_success 'requests are held while the sweep waits for sdbus' '
	$sdexec -r 0 --bg --waitable --label=t2419-held /bin/true &&
	wait_for_ps_state t2419-held Z &&
	flux module remove sdexec &&
	flux dmesg -C &&
	flux python -c "import flux; flux.Flux().rpc(\"sdbus.reconnect\").get()" &&
	flux module load sdexec &&
	$ps -r 0 -no "{state} {label}" >held-ps.out &&
	$wait -r 0 t2419-held &&
	test_debug "cat held-ps.out" &&
	grep "^Z t2419-held$" held-ps.out &&
	flux dmesg >held-dmesg.out &&
	test_debug "cat held-dmesg.out" &&
	grep "disconnect: user requested bus reconnect" held-dmesg.out &&
	grep "connected" held-dmesg.out &&
	grep "recover t2419-held" held-dmesg.out
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
# The seeded orphan was never started by sdexec, but it is adopted on the
# same terms as any other leftover: killable and waitable by label.
test_expect_success 'the seeded orphan is killable and waitable by label' '
	$kill -r 0 15 "$orphan" &&
	test_expect_code 143 $wait "$orphan"
'
test_expect_success 'remove modules' '
	flux module remove sdexec &&
	flux module remove sdbus
'
test_expect_success 'no records are left behind' '
	ls $recdir >records.out &&
	test_debug "cat records.out" &&
	test_must_be_empty records.out
'
test_expect_success 'clean up any residual test units' '
	systemctl --user stop "t2419-*" 2>/dev/null || true &&
	systemctl --user reset-failed "t2419-*" 2>/dev/null || true
'
test_done
