#!/bin/sh

test_description='Test resource drain/undrain'

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux $SIZE

# Usage: waitup N
#   where N is a count of online ranks
waitup () {
	run_timeout 5 flux python -c "import flux; print(flux.Flux().rpc(\"resource.monitor-waitup\",{\"up\":$1}).get())"
}
waitdown () {
	waitup $(($SIZE-$1))
}

has_resource_event () {
	flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}

drain_timestamp () {
	flux resource drain -o {timestamp} | tail -n 1 | awk '{ print $1 }'
}

test_expect_success 'wait for monitor to declare all ranks are up' '
	waitdown 0
'

test_expect_success 'flux resource drain: default lists some expected fields' '
	flux resource drain > default.out &&
	grep STATE default.out &&
	grep REASON default.out
'

test_expect_success 'flux resource drain: FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT works' '
	FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT="{nodelist} {nodelist}" \
		flux resource drain > default_override.out &&
	grep "NODELIST NODELIST" default_override.out
'

test_expect_success 'flux resource drain: FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT works w/ named format' '
	FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT=long \
		flux resource drain > default_override_named.out &&
	grep "RANKS" default_override_named.out
'

test_expect_success 'flux resource drain: --no-header works' '
	flux resource drain --no-header > default_no_header.out &&
	test_must_fail grep STATE default_no_header.out &&
	test_must_fail grep REASON default_no_header.out
'

test_expect_success 'drain works with no reason' '
	flux resource drain 1 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1
'

test_expect_success 'save original drain timestamp' '
	drain_timestamp > rank1.timestamp
'

test_expect_success 'resource.eventlog has one drain event' '
	test $(has_resource_event drain | wc -l) -eq 1 &&
	test $(flux resource status -s drain -no {ranks}) = "1"
'

test_expect_success 'reason can be added after node is drained' '
	flux resource drain 1 test_reason_01 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1
'

test_expect_success 'resource.eventlog has two drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'reason cannot be updated when already set' '
	test_expect_code 1 flux resource drain 1 test_reason_fail &&
	flux resource drain | test_must_fail grep test_reason_fail &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1
'

test_expect_success 'drain detects subset of already drained targets' '
	test_expect_code 1 flux resource drain 0-1 >drain-0-1.out 2>&1 &&
	test_debug "cat drain-0-1.out" &&
	grep "rank 1 already drained" drain-0-1.out &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1
'

test_expect_success 'drain suggests --force with existing reason' '
	test_must_fail flux resource drain 1 test_reason_updated \
		>update-failed.log 2>&1 &&
	test_debug "cat update-failed.log" &&
	grep -i 'use --force' update-failed.log
'

test_expect_success 'drain reason can be updated with --force' '
	flux resource drain --force 1 test_reason_updated &&
	flux resource drain | grep test_reason_updated
'

test_expect_success 'original drain timestamp is preserved' '
	test $(drain_timestamp) = $(cat rank1.timestamp)
'

test_expect_success 'drain timestamp and reason can be updated with -ff' '
	flux resource drain --force --force 1 test_reason_updated_again &&
	flux resource drain | grep test_reason_updated_again
'

test_expect_success 'original drain timestamp is preserved' '
	test $(drain_timestamp) != $(cat rank1.timestamp)
'

test_expect_success 'drain update mode does not change already drained rank' '
	flux resource drain --update 1 test_reason_notouch &&
	flux resource drain | test_must_fail grep test_reason_notouch
'

test_expect_success 'drain update mode works with idset' '
	flux resource drain --update 0-1 test_reason_update &&
	flux resource status -s drain -no {reason} | grep test_reason_update &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 2 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource undrain 0
'

test_expect_success 'drain works with idset' '
	flux resource drain 2-3 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 3 &&
	test $(flux resource status -s drain -no {ranks}) = "1-3"
'

test_expect_success 'reload resource module to simulate instance restart' '
	flux module remove sched-simple &&
	flux module reload resource &&
	waitdown 0 &&
	flux module load sched-simple
'

test_expect_success 'undrain one node' '
	flux resource undrain 3 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 2
'

test_expect_success 'two nodes are still drained' '
	test $(flux resource list -n -s down -o {nnodes}) -eq 2
'

test_expect_success 'undrain remaining nodes' '
	flux resource undrain 1-2 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 0
'

test_expect_success 'resource.eventlog has three undrain events' '
	test $(has_resource_event undrain | wc -l) -eq 3
'

test_expect_success 'reload resource module to simulate instance restart' '
	flux module remove sched-simple &&
	flux module reload resource &&
	waitdown 0 &&
	flux module load sched-simple
'

test_expect_success 'no nodes remain drained after restart' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0
'

test_expect_success 'drain one node' '
	flux resource drain 0 testing
'

test_expect_success 'exclude one node via configuration' '
	echo "resource.exclude = \"0\"" | flux config load
'

test_expect_success 'excluded node is no longer drained' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0
'

test_expect_success 'excluded node cannot be forcibly drained' '
	test_must_fail flux resource drain 0 reason
'

test_expect_success 'drain of idset with excluded and drained nodes fails' '
	flux resource drain 1 reason &&
	test_must_fail flux resource drain 0-1 another reason 2>multi.err &&
	test_debug "cat multi.err" &&
	grep "drained or excluded" multi.err
'

test_expect_success 'undrain/unexclude ranks' '
	flux resource undrain 1 &&
	echo "resource.exclude = \"\"" | flux config load
'

test_expect_success 'no nodes remain drained or excluded' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	test $(flux resource status -s exclude -no {nnodes}) -eq 0
'

test_expect_success 'drained rank subsequently excluded is ignored' '
	flux resource drain 1 this will be ignored &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux module remove sched-simple &&
	flux module remove resource &&
	echo resource.exclude = \"1\" | flux config load &&
	flux module load resource &&
	waitdown 0 &&
	flux module load sched-simple &&
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource list
'

test_expect_success 'unexclude ranks' '
	echo "resource.exclude = \"\"" | flux config load
'

test_expect_success 'no nodes remain drained or excluded' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	test $(flux resource status -s exclude -no {nnodes}) -eq 0
'

test_expect_success 'reload scheduler so it seems all ranks' '
	flux module reload sched-simple
'

test_expect_success 'undrain fails if rank not drained' '
	test_must_fail flux resource undrain 1 2>undrain_not.err &&
	grep "rank 1 not drained" undrain_not.err
'

test_expect_success 'drain fails if idset is empty' '
	test_must_fail flux resource drain "" 2>drain_empty.err &&
	grep "idset is empty" drain_empty.err
'

test_expect_success 'drain fails if idset is out of range' '
	test_must_fail flux resource drain "0-$SIZE" 2>drain_range.err &&
	grep "idset is out of range" drain_range.err
'

# Note: in test, drain `hostname` will drain all ranks since all ranks
#  are running on the same host
#
test_expect_success 'un/drain works with hostnames' '
	flux resource drain $(hostname) &&
	test $(flux resource list -n -s down -o {nnodes}) -eq $SIZE &&
	flux resource undrain $(hostname) &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 0
'

test_expect_success 'drain with no args lists currently drained targets' '
	flux resource drain 0 happy happy, joy joy &&
	flux resource drain > drain.out &&
	test_debug "cat drain.out" &&
	grep "happy happy, joy joy" drain.out
'

test_expect_success 'drain/undrain works on rank > 0' '
	flux exec -r 1 flux resource undrain 0 &&
	flux exec -r 1 flux resource drain 0 whee drained again
'

test_expect_success 'drain with no arguments works on rank > 0' '
	flux exec -r 1 flux resource drain
'

test_expect_success 'drain with no arguments works for guest' '
	FLUX_HANDLE_ROLEMASK=0x2 flux resource drain
'

drain_onrank() {
	local op=$1
	local nodeid=$2
	local target=$3
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.$op\",{\"targets\":$target, \"reason\":\"\"}, nodeid=$nodeid).get())"
}

test_expect_success 'resource.drain RPC fails on rank > 0' '
	test_must_fail drain_onrank drain 1 0 2>drain1.err &&
	grep -i "unknown service method" drain1.err
'

test_expect_success 'resource.undrain RPC fails on rank > 0' '
	test_must_fail drain_onrank undrain 1 0 2>undrain1.err &&
	grep -i "unknown service method" undrain1.err
'

test_expect_success 'drain works on allocated rank' '
	flux resource undrain $(flux resource status -s drain -no {ranks}) &&
	id=$(flux submit --wait-event=start sleep 300) &&
	rank=$(flux jobs -no {ranks} $id) &&
	flux resource drain $rank &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1 &&
	flux resource drain | grep draining &&
	flux cancel $id &&
	flux job wait-event $id clean &&
	flux resource drain | grep drained
'

test_expect_success 'flux resource drain differentiates drain/draining' '
	flux resource undrain $(flux resource status -s drain -no {ranks}) &&
	id=$(flux submit --wait-event=start sleep 300) &&
	rank=$(flux jobs -no {ranks} $id) &&
	flux resource drain $(hostname) &&
	test_debug "flux resource drain" &&
	test_debug "flux resource status" &&
	test $(flux resource status -s draining -no {ranks}) = "$rank" &&
	flux resource drain | grep draining &&
	flux cancel $id &&
	flux job wait-event $id clean &&
	test $(flux resource status -s drain -no {nnodes}) -eq ${SIZE}
'

test_expect_success 'flux resource drain supports --include' '
	flux resource drain -ni 0 >drain-include.output &&
	test_debug "cat drain-include.output" &&
	test $(wc -l <drain-include.output) -eq 1
'

test_expect_success 'flux resource drain works without scheduler loaded' '
	flux module unload sched-simple &&
	flux resource drain &&
	test $(flux resource status -s drain -no {nnodes}) -eq ${SIZE}
'

test_expect_success 'load scheduler again to free end-of-test resources' '
	flux module load sched-simple
'

test_done
