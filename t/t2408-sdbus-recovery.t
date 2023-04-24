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

test_under_flux 1 minimal

flux setattr log-stderr-level 1

# Usage: bus_get_manager_prop property
bus_get_manager_prop() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"path\":\"/org/freedesktop/systemd1\",\"interface\":\"org.freedesktop.DBus.Properties\",\"member\":\"Get\",\"params\":[\"org.freedesktop.systemd1.Manager\",\"$1\"]}).get_str())"
}

# Usage: bus_get_manager_prop property
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
test_expect_success NO_CHAIN_LINT 'background subscription fails' '
	pid=$(cat signals.pid) &&
	test_must_fail wait $pid &&
	grep "user request" signals.err
'
test_expect_success 'remove sdbus module' '
	flux module remove sdbus
'
test_done
