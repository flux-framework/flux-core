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
if ! busctl status >/dev/null; then
	skip_all="system dbus is not running"
	test_done
fi

test_under_flux 1 minimal

flux setattr log-stderr-level 1

# Usage: start test unit NAME (without service suffix)
start_test_unit() {
	local sleep=$(which sleep)
	flux exec \
	    --service sdexec \
	    --setopt SDEXEC_NAME="$1.service" \
	    $sleep 3600 &
}
# Usage: stop_test_unit NAME (without service suffix)
stop_test_unit() {
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

groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

test_expect_success 'load sdbus,sdexec modules' '
	flux module load --name sdbus-sys sdbus system &&
	flux module load sdbus &&
	flux module load sdexec
'
test_expect_success 'load sdmon module' '
	flux module load sdmon
'
test_expect_success 'make sure residual test units are not running' '
	stop_test_unit shell-t2412 || true &&
	stop_test_unit imp-shell-t2412 || true
'
test_expect_success 'wait for it to join the sdmon.online group' '
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'module stats units array is empty' '
	flux module stats sdmon | jq -e ".units == []"
'
test_expect_success 'run a systemd unit with imp-shell- prefix' '
	start_test_unit imp-shell-t2412
'
test_expect_success 'wait for module stats to show test unit' '
	wait_for_some 30
'
test_expect_success 'remove sdmon module' '
	flux module remove sdmon
'
# removing the module triggers a disconnect that causes a group leave
test_expect_success 'wait for it to leave the sdmon.online group' '
	run_timeout 30 $groups waitfor --count=0 sdmon.online
'
test_expect_success 'load sdmon module' '
	flux module load sdmon
'
test_expect_success 'wait for module stats to show test unit' '
	wait_for_some 30
'
test_expect_success 'stop the unit' '
	stop_test_unit imp-shell-t2412
'
test_expect_success 'wait for sdmon to join the sdmon.online group' '
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'run a systemd unit with shell- prefix' '
	start_test_unit shell-t2412
'
test_expect_success 'wait for module stats to show test unit' '
	wait_for_some 30
'
test_expect_success 'stop the unit' '
	stop_test_unit shell-t2412
'
test_expect_success 'wait for module stats stop showing test unit' '
	wait_for_none 30
'
test_expect_success 'remove sdmon module' '
	flux module remove sdmon
'
test_expect_success 'remove sdexec,sdbus modules' '
	flux module remove sdexec &&
	flux module remove sdbus &&
	flux module remove sdbus-sys
'
test_done
