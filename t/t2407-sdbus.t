#!/bin/sh
# ci=system

test_description='Test flux systemd sd-bus bridge standalone'

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

test_under_flux 2 minimal

flux setattr log-stderr-level 3

#
# N.B. ListUnitsByPatterns response payload is a 'params' array whose first
# and only item (".params[0]") is an array of units.  The jq(1) expression
# ".params[0][][0]" extracts field 0 of each unit array entry (unit name)
# and prints one entry per line, ".params[0][][1]" extracts field 1 of each
# unit entry (description) and prints one entry per line, etc.
#

# Usage: bus_list_units PATTERN
bus_list_units() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"ListUnitsByPatterns\",\"params\":[[],[\"$1\"]]}).get_str())"
}

# Usage: bus_list_units_parsed PATTERN FIELDNUM
bus_list_units_parsed() {
    bus_list_units "$1" >tmp.json || return 1
    jq -r ".params[0][][$2]" <tmp.json
}

# Usage: bus_count_units PATTERN
bus_count_units() {
    bus_list_units_parsed "$1" 0 >tmp.list || return 1
    wc -l <tmp.list
}

# Usage: bus_wait_for_unit_count TRIES COUNT PATTERN
bus_wait_for_unit_count() {
    local tries=$1
    while test $tries -gt 0; do
	test $tries -eq $1 || sleep 1
        test $(bus_count_units $3) -eq $2 && return 0
        tries=$(($tries-1))
    done
    return 1
}

# Usage: bus_reset_failed_unit name
bus_reset_failed_unit() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"ResetFailedUnit\",\"params\":[\"$1\"]}).get_str())"
}

# Usage: bus_stop_unit name
bus_stop_unit() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"StopUnit\",\"params\":[\"$1\",\"fail\"]}).get_str())"
}

# Usage: bus_kill_unit name signum
bus_kill_unit() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"KillUnit\",\"params\":[\"$1\",\"all\",$2]}).get_str())"
}

# Usage: bus_get_prop_all interface name
bus_get_prop_all () {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"path\":\"/org/freedesktop/systemd1/unit/$2\",\"interface\":\"org.freedesktop.DBus.Properties\",\"member\":\"GetAll\",\"params\":[\"$1\"]}).get_str())"
}

# Usage: bus_get_prop interface name property
bus_get_prop () {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"path\":\"/org/freedesktop/systemd1/unit/$2\",\"interface\":\"org.freedesktop.DBus.Properties\",\"member\":\"Get\",\"params\":[\"$1\",\"$3\"]}).get_str())"
}

# Usage: bus_start_simple name description remain cmd arg1
# where remain=True|False and cmd has exactly one argument
bus_start_simple() {
    local cmd=$(which $4) || return 1
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"StartTransientUnit\",\"params\":[\"$1\",\"fail\",[ [\"Description\",[\"s\",\"$2\"]], [\"RemainAfterExit\",[\"b\",$3]], [\"ExecStart\",[\"a(sasb)\",[[\"$cmd\",[\"$4\",\"$5\"],False]]]] ], []]}).get_str())"
}

bus_call_unknown_member() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"member\":\"UnknownMember\",\"params\":[]}).get_str())"
}

bus_call_unknown_interface() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{\"interface\":\"org.freedesktop.DBus.unknown\",\"member\":\"StopUnit\",\"params\":[\"$1\",\"fail\"]}).get_str())"
}

bus_call_malformed() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.call\",{}).get_str())"
}

bus_subscribe_notstreaming() {
    flux python -c "import flux; print(flux.Flux().rpc(\"sdbus.subscribe\",{}).get_str())"
}

# Usage: bus_subscribe_cancel matchtag
bus_subscribe_cancel () {
    flux python -c "import flux; flux.Flux().rpc(\"sdbus.subscribe-cancel\",{\"matchtag\":$1},flags=flux.constants.FLUX_RPC_NORESPONSE)"
}

test_expect_success 'enable sdbus-debug in configuration' '
	flux config load <<-EOT
	[systemd]
	sdbus-debug = true
	EOT
'
test_expect_success 'load sdbus module' '
	flux module load sdbus
'
test_expect_success 'sdbus reconfig fails with bad sdbus-debug value' '
	test_must_fail flux config load <<-EOT 2>config.err &&
	[systemd]
	sdbus-debug = 42
	EOT
	grep "Expected true or false" config.err
'
test_expect_success 'restore correct config in case we reload later' '
	flux config load <<-EOT
	[systemd]
	sdbus-debug = true
	EOT
'

test_expect_success 'sdbus list-units works' '
	count=$(bus_count_units "*") &&
	echo Listed $count units >&2
'

clean_unit() {
	local unit=$1
	echo Cleaning up $unit
	bus_reset_failed_unit $unit
	bus_stop_unit $unit
	return 0
}

test_expect_success 'reset/unload any flux-t2406 units from prior runs' '
	bus_list_units_parsed "flux-t2406-*" 0 >oldunits.out &&
	for unit in $(cat oldunits.out); do clean_unit $unit; done
'
test_expect_success 'there are no flux-t2406 units loaded' '
	bus_wait_for_unit_count 10 0 "flux-t2406-*"
'
# script usage: subscribe.py [path-glob]
test_expect_success 'create subscribe script' '
	cat >subscribe.py <<-EOT &&
	import sys
	import flux
	handle = flux.Flux()
	sub = {"member":"PropertiesChanged","interface":"org.freedesktop.DBus.Properties"}
	if len(sys.argv) > 1:
	    sub["path"] = sys.argv[1]
	fut = handle.rpc("sdbus.subscribe",sub,flags=flux.constants.FLUX_RPC_STREAMING)
	while True:
	    print(fut.get_str())
	    sys.stdout.flush()
	    fut.reset()
	EOT
	chmod +x subscribe.py
'
test_expect_success NO_CHAIN_LINT 'subscribe to flux-t2406-[2-9]* signals' '
	flux python ./subscribe.py \
	    /org/freedesktop/systemd1/unit/flux-t2406-[2-9]* \
	    >signals.out 2>signals.err &
	echo $! >signals.pid
'
test_expect_success NO_CHAIN_LINT 'subscribe to all signals' '
	flux python ./subscribe.py \
	    >signals_all.out 2>signals_all.err &
	echo $! >signals_all.pid
'
test_expect_success 'StopUnit unknown.service fails' '
	test_must_fail bus_stop_unit unknown.service 2>stop_unknown.err &&
	grep "not loaded" stop_unknown.err
'
test_expect_success 'KillUnit unknown.service fails' '
	test_must_fail bus_kill_unit unknown.service 15 2>kill_unknown.err &&
	grep "not loaded" kill_unknown.err
'
test_expect_success 'ResetFailedUnit unknown.service fails' '
	test_must_fail bus_reset_failed_unit unknown.service \
	    2>rst_unknown.err &&
	grep "not loaded" rst_unknown.err
'
test_expect_success 'StartTransientUnit RemainAfterExit=true cmd=true' '
	bus_start_simple flux-t2406-1.service unit1 True true dummy
'
test_expect_success 'ListUnitsByPatterns shows transient unit 1' '
	bus_wait_for_unit_count 10 1 flux-t2406-1.service
'
test_expect_success 'StopUnit works on transient unit 1' '
	bus_stop_unit flux-t2406-1.service
'
test_expect_success 'ListUnitsByPatterns does not show transient unit 1' '
	bus_wait_for_unit_count 10 0 flux-t2406-1.service
'
test_expect_success 'StartTransientUnit RemainAfterExit=false cmd=false' '
	bus_start_simple flux-t2406-2.service unit2 False false dummy
'
test_expect_success 'ListUnitsByPatterns shows transient unit 2' '
	bus_wait_for_unit_count 10 1 flux-t2406-2.service
'
test_expect_success 'ResetFailedUnit works on that unit' '
	bus_reset_failed_unit flux-t2406-2.service
'
test_expect_success 'ListUnitsByPatterns does not show transient unit 2' '
	bus_wait_for_unit_count 10 0 flux-t2406-2.service
'
test_expect_success 'StartTransientUnit RemainAfterExit=false cmd=sleep 60' '
	bus_start_simple flux-t2406-3.service unit3 False sleep 60
'
test_expect_success 'ListUnitsByPatterns shows transient unit 3' '
	bus_wait_for_unit_count 10 1 flux-t2406-3.service
'
test_expect_success 'GetAll shows properties of transient unit 3' '
	bus_get_prop_all org.freedesktop.systemd1.Service \
	    flux-t2406-3.service >prop3_all.out
'
test_expect_success 'GetAll returned MainPID property and it is valid' '
	jq <prop3_all.out .params[0].MainPID[1] >pid3.out &&
	kill -0 $(cat pid3.out)
'
test_expect_success 'Get MainPID also works' '
	bus_get_prop org.freedesktop.systemd1.Service \
	    flux-t2406-3.service MainPID >prop3.out
'
test_expect_success 'Get returned the same MainPID value' '
	jq <prop3.out .params[0][1] >pid3.out2 &&
	test_cmp pid3.out pid3.out2
'
test_expect_success 'KillUnit sends SIGTERM (15) to that unit' '
	bus_kill_unit flux-t2406-3.service 15
'
test_expect_success 'ListUnitsByPatterns does not show transient unit 3' '
	bus_wait_for_unit_count 10 0 flux-t2406-3.service
'
test_expect_success 'StartTransientUnit RemainAfterExit=true cmd=true' '
	bus_start_simple flux-t2406-4.service "This is a test" True true dummy
'
test_expect_success 'ListUnitsByPatterns shows transient unit 4' '
	bus_wait_for_unit_count 10 1 flux-t2406-4.service
'
test_expect_success 'ListUnitsByPattern shows transient unit 4 description' '
	bus_list_units_parsed flux-t2406-4.service 1 >listdesc.out &&
	grep "This is a test" listdesc.out
'
test_expect_success 'StopUnit works on that unit' '
	bus_stop_unit flux-t2406-4.service
'
test_expect_success 'ListUnitsByPatterns does not show transient unit 4' '
	bus_wait_for_unit_count 10 0 flux-t2406-4.service
'
test_expect_success 'calling an unknown member fails' '
	test_must_fail bus_call_unknown_member 2>unknown_member.err &&
	grep "unknown member" unknown_member.err
'
test_expect_success 'calling an unknown interface fails' '
	test_must_fail bus_call_unknown_interface 2>unknown_interface.err &&
	grep "unknown interface" unknown_interface.err
'
test_expect_success 'malformed sdbus.call request fails' '
	test_must_fail bus_call_malformed 2>malformed.err &&
	grep "malformed request" malformed.err
'
test_expect_success 'non-streaming sdbus.subscribe fails' '
	test_must_fail bus_subscribe_notstreaming 2>subscribe.err &&
	grep "Protocol error" subscribe.err
'
test_expect_success 'send a sdbus.subscribe-cancel on random matchtag for fun' '
	(FLUX_HANDLE_TRACE=1 bus_subscribe_cancel 424242)
'
test_expect_success NO_CHAIN_LINT 'terminate background subscribers' '
	kill -15 $(cat signals_all.pid) &&
	kill -15 $(cat signals.pid)
'
test_expect_success NO_CHAIN_LINT 'PropertiesChanged signals were received' '
	test $(grep PropertiesChanged signals_all.out | wc -l) -gt 0
'
test_expect_success NO_CHAIN_LINT 'all units triggered signals' '
	test $(grep flux-t2406-1 signals_all.out | wc -l) -gt 0 &&
	test $(grep flux-t2406-2 signals_all.out | wc -l) -gt 0 &&
	test $(grep flux-t2406-3 signals_all.out | wc -l) -gt 0 &&
	test $(grep flux-t2406-4 signals_all.out | wc -l) -gt 0
'
test_expect_success NO_CHAIN_LINT 'subscription filter worked' '
	test $(grep flux-t2406-1 signals.out | wc -l) -eq 0 &&
	test $(grep flux-t2406-2 signals.out | wc -l) -gt 0 &&
	test $(grep flux-t2406-3 signals.out | wc -l) -gt 0 &&
	test $(grep flux-t2406-4 signals.out | wc -l) -gt 0
'
# Test restriction of RPCs to sdbus on rank 0 from rank 1
# N.B. requests are forwarded upstream b/c sdbus is not loaded on rank 1
test_expect_success 'subscribe from rank 1 is restricted' '
        test_must_fail flux exec -r 1 flux python ./subscribe.py 2>sub1.err &&
	grep "not allowed" sub1.err
'
test_expect_success 'create list script' '
	cat >list.py <<-EOT &&
	import sys
	import flux
	print(flux.Flux().rpc(sys.argv[1] + ".call",{"member":"ListUnitsByPatterns","params":[[],["*"]]}).get_str())
	EOT
	chmod +x list.py
'
test_expect_success 'list from rank 0 is allowed' '
	flux python ./list.py sdbus >/dev/null
'
test_expect_success 'list from rank 1 is restricted' '
	test_must_fail flux exec -r 1 \
	    flux python ./list.py sdbus 2>list1.err &&
	grep "not allowed" list1.err
'

test_expect_success 'load sdbus-sys module' '
	flux module load --name sdbus-sys sdbus system
'
test_expect_success 'list system units works' '
	flux python ./list.py sdbus-sys >/dev/null
'
test_expect_success 'remove sdbus-sys module' '
	flux module remove sdbus-sys
'
test_expect_success 'remove sdbus module' '
	flux module remove sdbus
'


test_done
