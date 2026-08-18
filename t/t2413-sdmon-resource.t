#!/bin/sh
# ci=system

test_description='Test systemd monitoring resource integration'

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

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
enable = true
[exec]
service = "sdexec"
EOT

test_under_flux 1 full --config-path=$(pwd)/config -Slog-stderr-level=1

testname="t2413-$$"

groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

# The user systemd instance is shared with other tests running in parallel, so
# unit names carry a unique per-run suffix and sdmon/sdexec are narrowed to
# test-owned globs (sys_glob/recover_glob).  The sdbus-sys bridge is pointed at
# the user bus so a flux-prolog-* unit started with systemd-run --user is what
# sdmon sees as a system unit, without privileged system bus access.
prolog="flux-prolog-${testname}"

# Usage: start_sys_unit NAME (without .service suffix)
start_sys_unit() {
	local sleep=$(which sleep)
	systemd-run --user --unit="$1.service" --service-type=simple $sleep 3600
}
# Usage: stop_sys_unit NAME (without .service suffix)
stop_sys_unit() {
	systemctl --user stop $1
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

test_expect_success 'flux module load sdmon fails on unknown module option' '
	flux module remove sdmon &&
	test_must_fail flux module load sdmon unknown
'
# Replace the rc-loaded systemd module stack (whose default globs match any
# test's units on the shared bus) with one narrowed to this test.
test_expect_success 'reload systemd module stack with test-owned globs' '
	flux module remove sdexec &&
	flux module remove sdbus-sys &&
	flux module load --name sdbus-sys sdbus &&
	flux module load sdmon sys_glob="flux-*-${testname}*" &&
	flux module load sdexec recover_glob="shell-*-${testname}*"
'
test_expect_success 'wait for sdmon.online group' '
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'wait for online resource event' '
	run_timeout 30 flux resource eventlog --wait=online
'
test_expect_success 'start a test unit that looks like a prolog' '
	start_sys_unit "$prolog" &&
	systemctl --user is-active "${prolog}.service"
'
test_expect_success 'wait for module stats to show test unit' '
	wait_for_some 30
'
# Reload sdmon so its initial list sees the running unit and it withholds the
# group join; reload sdexec after it so the one-shot user-clean signal reaches
# the new sdmon instance; reload resource so it re-monitors the group.
test_expect_success 'clear dmesg, then reload sdmon, sdexec, resource' '
	flux dmesg -C &&
	flux module remove sched-simple &&
	flux module remove resource &&
	flux module remove sdexec &&
	flux module reload sdmon sys_glob="flux-*-${testname}*" &&
	flux module load sdexec recover_glob="shell-*-${testname}*" &&
	flux module load resource &&
	flux module load sched-simple
'
test_expect_success 'the test unit was flagged as needing cleanup' '
	flux dmesg -H | grep "${prolog}.service needs cleanup"
'
test_expect_success 'node is offline while the test unit runs' '
	test -z "$($groups get sdmon.online)"
'
test_expect_success 'stop test unit' '
	stop_sys_unit "$prolog"
'
test_expect_success 'wait for module stats to show nothing' '
	wait_for_none 30
'
test_expect_success 'wait for sdmon.online group' '
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'wait for online resource event' '
	run_timeout 30 flux resource eventlog --wait=online
'
test_expect_success 'unit cleanup was logged' '
	flux dmesg -H >dmesg.out &&
	grep "cleanup complete" dmesg.out
'
test_expect_success 'clean up any residual test units' '
	stop_sys_unit "$prolog" 2>/dev/null || true &&
	systemctl --user reset-failed "${prolog}.service" 2>/dev/null || true
'
test_done
