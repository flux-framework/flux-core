#!/bin/sh
# ci=system

test_description='Test flux systemd monitoring'

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

test_under_flux 1 minimal -Slog-stderr-level=1

testname="t2412-$$"

# sdmon monitors the *system* systemd instance (housekeeping/prolog/epilog),
# matching the glob flux-*.  Rather than create real system units (which needs
# privilege), point the "sdbus-sys" bridge at the *user* bus so a flux-prolog-*
# unit started with systemd-run --user is what sdmon sees as a system unit.
# That bus is shared with other tests running in parallel, so unit names carry
# a unique per-run suffix and sdmon monitoring is narrowed to it (sys_glob).

# Usage: start_sys_unit NAME (without .service suffix)
start_sys_unit() {
	local sleep=$(which sleep)
	systemd-run --user --unit="$1.service" --service-type=simple $sleep 3600
}
# Usage: stop_sys_unit NAME (without .service suffix)
stop_sys_unit() {
	systemctl --user stop $1
}
reset_sys_unit() {
	systemctl --user reset-failed $1
}

# Usage: wait_for_none MAXSEC
wait_for_none() {
	local retry=$(($1*10))
	while ! flux module stats sdmon | jq -e ".units == []"; do
	    sleep 0.1
	    retry=$(($retry-1))
	    test $retry -gt 0 || exit 1
	done
}
# Usage: wait_for_some MAXSEC
wait_for_some() {
	local retry=$(($1*10))
	while flux module stats sdmon | jq -e ".units == []"; do
	    sleep 0.1
	    retry=$(($retry-1))
	    test $retry -gt 0 || exit 1
	done
}

# Tell sdmon on the local rank that the user bus is clean.  Normally sdexec
# sends this after its startup sweep; here we drive the gate directly so the
# test does not depend on sdexec.
send_user_clean() {
	flux python -c \
	    "import flux; flux.Flux().rpc(\"sdmon.user-clean\",{},nodeid=0).get()"
}

groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

# The prolog-like system unit must exist before sdmon starts so its initial
# list picks it up and the node begins offline.
prolog="flux-prolog-${testname}"

test_expect_success 'enable sdbus-debug in configuration' '
	flux config load <<-EOT
	[systemd]
	sdbus-debug = true
	EOT
'
# Load the "sdbus-sys" bridge on the user bus (no "system" arg) so this
# unprivileged test can create the flux-* units sdmon expects on the system bus.
test_expect_success 'load sdbus-sys bridge (on the user bus for testing)' '
	flux module load --name sdbus-sys sdbus
'
test_expect_success 'seed a running system unit before sdmon starts' '
	start_sys_unit "$prolog" &&
	systemctl --user is-active "${prolog}.service"
'
test_expect_success 'load sdmon module' '
	flux module load sdmon sys_glob="flux-*-${testname}*"
'
test_expect_success 'sdmon lists the running system unit' '
	wait_for_some 30 &&
	flux module stats sdmon | jq -e \
	    "[.units[].name] | index(\"${prolog}.service\") != null"
'
# The node stays offline until BOTH gates clear: system units drained and
# sdexec reports the user bus clean.  A running system unit holds it offline
# even once user-clean has been received.
test_expect_success 'user-clean alone does not bring the node online' '
	send_user_clean &&
	test -z "$($groups get sdmon.online)"
'
test_expect_success 'draining system units alone does not bring it online' '
	flux module reload sdmon sys_glob="flux-*-${testname}*" &&
	stop_sys_unit "$prolog" &&
	wait_for_none 30 &&
	test -z "$($groups get sdmon.online)"
'
# With the system units drained AND user-clean received, sdmon joins online.
test_expect_success 'node comes online once both gates clear' '
	send_user_clean &&
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
# Removing sdmon disconnects it, which leaves the group.
test_expect_success 'removing sdmon leaves the online group' '
	flux module remove sdmon &&
	run_timeout 30 $groups waitfor --count=0 sdmon.online
'
# A unit that appears after sdmon is online (via a property update, not the
# initial list) is still tracked.
test_expect_success 'reload clean and confirm online' '
	flux module load sdmon sys_glob="flux-*-${testname}*" &&
	send_user_clean &&
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'a unit appearing later is tracked' '
	start_sys_unit "$prolog" &&
	wait_for_some 30
'
test_expect_success 'and drops from the list when it stops' '
	stop_sys_unit "$prolog" &&
	wait_for_none 30
'
test_expect_success 'sdmon rejects an unknown module option' '
	flux module remove sdmon &&
	test_must_fail flux module load sdmon unknown
'
test_expect_success 'remove modules' '
	flux module remove sdmon 2>/dev/null || true &&
	flux module remove sdbus-sys
'
test_expect_success 'clean up any residual test units' '
	stop_sys_unit "$prolog" 2>/dev/null || true &&
	reset_sys_unit "$prolog" 2>/dev/null || true
'
test_done
