#!/bin/sh
# ci=system

test_description='Test flux systemd sd-bus bridge recovery'

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

mkdir -p config
cat >config/config.toml <<EOF
[systemd]
sdbus-debug = true
EOF

test_under_flux 1 minimal --config-path=$(pwd)/config

flux setattr log-stderr-level 1

# Usage: bus_get_manager_prop property
bus_get_manager_prop() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"path\":\"/org/freedesktop/systemd1\",\"interface\":\"org.freedesktop.DBus.Properties\",\"member\":\"Get\",\"params\":[\"org.freedesktop.systemd1.Manager\",\"$1\"]}).get_str())"
}

# Usage: bus_reconnect
bus_reconnect() {
    flux python -c "import flux; flux.Flux().rpc(\"sdbus.reconnect\",{}).get()"
}

test_expect_success 'load sdbus module' '
	flux module load sdbus
'
test_expect_success 'create subscribe script' '
	cat >subscribe.py <<-EOT &&
	import sys
	import flux
	handle = flux.Flux()
	fut = handle.rpc("sdbus.subscribe",{},flags=flux.constants.FLUX_RPC_STREAMING)
	while True:
	    print(fut.get_str())
	    sys.stdout.flush()
	    fut.reset()
	EOT
	chmod +x subscribe.py
'
test_expect_success 'get systemd version' '
	bus_get_manager_prop Version
'
test_expect_success 'clear the broker ring buffer' '
	flux dmesg -C
'
# This should trigger a 2s backoff before reconnect is attempted
test_expect_success 'force bus reconnect' '
	bus_reconnect
'
test_expect_success NO_CHAIN_LINT 'initiate subscription in the background' '
        flux python ./subscribe.py >signals.out 2>signals.err &
        echo $! >signals.pid
'
# Requests during the backoff period are delayed but still work
test_expect_success 'get systemd version works during despite reconnect' '
	bus_get_manager_prop Version
'
test_expect_success 'print logs' '
	flux dmesg -H | grep sdbus
'
test_expect_success 'force another bus reconnect' '
	bus_reconnect
'
test_expect_success 'background subscription fails with EAGAIN' '
	pid=$(cat signals.pid) &&
	test_must_fail wait $pid &&
	grep "Errno 11" signals.err
'
test_expect_success NO_CHAIN_LINT 'initiate another subscription' '
        flux python ./subscribe.py >signals2.out 2>signals2.err &
        echo $! >signals2.pid
'
# There will be another 2s delay while sdbus reconnects
test_expect_success 'get systemd version to ensure reconnect has occurred' '
	bus_get_manager_prop Version
'
test_expect_success 'remove sdbus module' '
	flux module remove sdbus
'
test_expect_success 'background subscription fails with ENOSYS' '
	pid=$(cat signals2.pid) &&
	test_must_fail wait $pid &&
	grep "Errno 38" signals2.err
'
test_done
