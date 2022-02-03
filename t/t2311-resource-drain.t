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

test_expect_success 'wait for monitor to declare all ranks are up' '
	waitdown 0
'

test_expect_success 'drain works with no reason' '
	flux resource drain 1 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1
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

test_expect_success 'reason can be updated after node is drained' '
	flux resource drain 1 test_reason_01 &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 1 &&
	test $(flux resource status -s drain -no {reason}) = "test_reason_01"
'

test_expect_success 'resource.eventlog has three drain events' '
	test $(has_resource_event drain | wc -l) -eq 3
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

test_expect_success 'resource.eventlog has two undrain events' '
	test $(has_resource_event undrain | wc -l) -eq 2
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

test_done
