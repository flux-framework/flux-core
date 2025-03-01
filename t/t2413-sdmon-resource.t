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

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
enable = true
[exec]
service = "sdexec"
EOT

test_under_flux 1 full --config-path=$(pwd)/config

flux setattr log-stderr-level 1

groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

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

test_expect_success 'make sure residual test units are not running' '
        stop_test_unit shell-t2413 2>/dev/null || true
'
test_expect_success 'wait for sdmon.online group' '
	run_timeout 30 $groups waitfor --count=1 sdmon.online
'
test_expect_success 'wait for online resource event' '
	run_timeout 30 flux resource eventlog --wait=online
'
test_expect_success 'start a test unit that looks like a job shell' '
	start_test_unit shell-t2413
'
test_expect_success 'wait for module stats to show test unit' '
        wait_for_some 30
'
test_expect_success 'clear dmesg, then reload sdmin, resource, sched-simple' '
	flux dmesg -C &&
	flux module remove sched-simple &&
	flux module remove resource &&
	flux module reload sdmon &&
	flux module load resource &&
	flux module load sched-simple
'
test_expect_success 'stop test unit' '
	stop_test_unit shell-t2413
'
test_expect_success 'wait for module stats to show nothing' '
        wait_for_none 30
'
test_expect_success 'wait for online resource event' '
	run_timeout 30 flux resource eventlog --wait=online
'
test_expect_success 'unit cleanup was logged' '
	flux dmesg -H >dmesg.out &&
	grep "shell-t2413.service needs cleanup" dmesg.out &&
	grep "cleanup complete" dmesg.out
'

test_done
