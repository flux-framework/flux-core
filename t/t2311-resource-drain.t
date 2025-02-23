#!/bin/sh

test_description='Test resource drain/undrain'

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux $SIZE full --test-hosts=fake[0-3]


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
# fake hostnames match the ones set on the broker command line
test_expect_success 'load fake resources' '
	flux module remove sched-simple &&
	flux R encode -r 0-3 -c 0-1 -H fake[0-3] >R &&
	flux resource reload R &&
	flux module load sched-simple
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
	grep -i "use --force" update-failed.log
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
	flux module reload resource noverify &&
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

test_expect_success 'undrain remaining nodes with a reason' '
	flux resource undrain 1-2 "reason for undrain" &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 0
'

test_expect_success 'resource.eventlog has three undrain events' '
	test $(has_resource_event undrain | wc -l) -eq 3
'
test_expect_success 'final undrain event has a reason' '
	flux resource eventlog -H | tail -1 | grep "reason for undrain"
'
test_expect_success 'reload resource module to simulate instance restart' '
	flux module remove sched-simple &&
	flux module reload resource noverify &&
	waitdown 0 &&
	flux module load sched-simple
'

test_expect_success 'no nodes remain drained after restart' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0
'

test_expect_success 'drain one node' '
	flux resource drain 0 testing
'
test_expect_success 'reload resource module with one node excluded' '
	flux module remove sched-simple &&
	flux module remove resource &&
	echo "resource.exclude = \"0\"" | flux config load &&
	flux module load resource noverify &&
	waitdown 0 &&
	flux module load sched-simple
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

test_expect_success 'undrain ranks' '
	flux resource undrain 1
'
test_expect_success 'reload resource module with no nodes excluded' '
	flux module remove sched-simple &&
	flux module remove resource &&
	echo "resource.exclude = \"\"" | flux config load &&
	flux module load resource noverify &&
	waitdown 0 &&
	flux module load sched-simple
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
	flux module load resource noverify &&
	waitdown 0 &&
	flux module load sched-simple &&
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource list
'

test_expect_success 'reload resource module with no nodes excluded' '
	flux module remove sched-simple &&
	flux module remove resource &&
	echo "resource.exclude = \"\"" | flux config load &&
	flux module load resource noverify &&
	waitdown 0 &&
	flux module load sched-simple
'

test_expect_success 'no nodes remain drained or excluded' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	test $(flux resource status -s exclude -no {nnodes}) -eq 0
'

test_expect_success 'undrain fails if rank not drained' '
	test_must_fail flux resource undrain 1 2>undrain_not.err &&
	grep ".*rank 1.* not drained" undrain_not.err
'

test_expect_success 'undrain fails if any rank not drained' '
	flux resource drain 0 &&
	test_must_fail flux resource undrain 0-1 2>undrain_0-1_not.err &&
	test_debug "cat undrain_0-1_not.err" &&
	grep ".*rank 1.* not drained" undrain_0-1_not.err
'

test_expect_success 'undrain reports multiple ranks not drained' '
	test_must_fail flux resource undrain 0-2 2>undrain_0-2_not.err &&
	test_debug "cat undrain_0-2_not.err" &&
	grep ".*ranks 1-2.* not drained" undrain_0-2_not.err
'

test_expect_success 'undrain --force works even if a target is not drained' '
	flux resource undrain --force 0-1
'

test_expect_success 'undrain --force returns success when no targets drained' '
	flux resource undrain --force 0-1
'

undrain_bad_mode() {
	flux python -c 'import flux; \
	  flux.Flux().rpc("resource.undrain", \
			  {"targets": "0", "mode": "foo"}).get()'
}

test_expect_success 'undrain RPC rejects invalid mode' '
	test_must_fail undrain_bad_mode 2>undrain_bad_mode.err &&
	grep "invalid undrain mode" undrain_bad_mode.err
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
	flux resource drain fake[2-3] &&
	test $(flux resource list -n -s down -o {nnodes}) -eq 2 &&
	flux resource undrain fake[2-3] &&
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
	flux resource drain fake0 &&
	test_debug "flux resource drain" &&
	test_debug "flux resource status" &&
	test $(flux resource status -s draining -no {ranks}) = "$rank" &&
	flux resource drain | grep draining &&
	flux cancel $id &&
	flux job wait-event $id clean &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1
'

test_expect_success 'flux resource drain supports --include' '
	flux resource drain -ni 0 >drain-include.output &&
	test_debug "cat drain-include.output" &&
	test $(wc -l <drain-include.output) -eq 1
'

test_expect_success 'flux resource drain works without scheduler loaded' '
	flux module unload sched-simple &&
	flux resource drain &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1
'

test_expect_success 'resource can replay eventlog with pre v0.62 events' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713893408.8647039,"name":"resource-init","context":{"restart":false,"drain":{},"online":"","exclude":""}}
	{"timestamp":1713893408.8673074,"name":"resource-define","context":{"method":"configuration"}}
	{"timestamp":1713893411.0698946,"name":"online","context":{"idset":"0"}}
	{"timestamp":1713893411.7209313,"name":"online","context":{"idset":"1"}}
	{"timestamp":1713893412.2919452,"name":"online","context":{"idset":"2-3"}}
	{"timestamp":1713905771.5895882,"name":"offline","context":{"idset":"1-3"}}
	{"timestamp":1713905772.5933509,"name":"resource-init","context":{"restart":true,"drain":{},"online":"","exclude":""}}
	{"timestamp":1713905772.5948801,"name":"resource-define","context":{"method":"configuration"}}
	{"timestamp":1713906350.984611,"name":"drain","context":{"idset":"2","reason":"smurfs","overwrite":0}}
	{"timestamp":1713906351.000000,"name":"drain","context":{"idset":"2","reason":"underwear","overwrite":1}}
	{"timestamp":1713906369.9485908,"name":"resource-init","context":{"restart":true,"drain":{"2":{"timestamp":1713906351.000000,"reason":"underwear"}},"online":"","exclude":""}}
	EOT
	flux module reload resource noverify
'
test_expect_success 'nodes drained in old eventlog are drained after replay' '
	flux resource drain -n -o "{ranks} {reason}" >legacydrain.out &&
	cat >legacydrain.exp<<-EOT &&
	2 underwear
	EOT
	test_cmp legacydrain.exp legacydrain.out
'

test_expect_success 'resource can replay eventlog with bad ranks' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906351.000000,"name":"drain","context":{"idset":"42","nodelist":"fake42","reason":"","overwrite":0}}
	EOT
	flux module reload resource noverify
'

test_expect_success 'no nodes are drained after replay' '
	test -z "$(flux resource drain -n -o {nnodes})"
'

test_expect_success 'reload resource with two nodes drained' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906350.984611,"name":"drain","context":{"idset":"1-2","nodelist":"fake[1-2]","reason":"uvula","overwrite":0}}
	EOT
	flux module reload resource noverify
'

test_expect_success 'the correct two nodes are drained after replay' '
	flux resource drain -n -o "{ranks} {reason}" >drainhosts.out &&
	cat >drainhosts.exp<<-EOT &&
	1-2 uvula
	EOT
	test_cmp drainhosts.exp drainhosts.out
'

test_expect_success 'reload resource with two nodes remapped' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906350.984611,"name":"drain","context":{"idset":"1-2","nodelist":"fake[2-3]","reason":"uvula","overwrite":0}}
	EOT
	flux module reload resource noverify
'

test_expect_success 'the remapped nodes are drained after replay' '
	flux resource drain -n -o "{ranks} {reason}" >drainhosts2.out &&
	cat >drainhosts2.exp<<-EOT &&
	2-3 uvula
	EOT
	test_cmp drainhosts2.exp drainhosts2.out
'

test_expect_success 'reload resource with two nodes remapped, one bad host' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906350.984611,"name":"drain","context":{"idset":"1-2","nodelist":"fake[3-4]","reason":"uvula","overwrite":0}}
	EOT
	flux module reload resource noverify
'

test_expect_success 'the remapped nodes are drained after replay' '
	flux resource drain -n -o "{ranks} {reason}" >drainhosts3.out &&
	cat >drainhosts3.exp<<-EOT &&
	3 uvula
	EOT
	test_cmp drainhosts3.exp drainhosts3.out
'

test_expect_success 'a malformed event in the eventlog prevents loading' '
	flux module remove -f resource &&
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{}
	EOT
	test_must_fail flux module load resource noverify
'
test_expect_success 'a malformed drain context in the eventlog prevents loading' '
	flux module remove -f resource &&
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906350.984611,"name":"drain","context":{}}
	EOT
	test_must_fail flux module load resource noverify
'
test_expect_success 'a malformed undrain context in the eventlog fails' '
	flux module remove -f resource &&
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":1713906350.984611,"name":"undrain","context":{}}
	EOT
	test_must_fail flux module load resource noverify
'
test_expect_success '' '
	flux module remove -f resource &&
	flux kvs unlink resource.eventlog &&
	flux module load resource noverify
'

test_expect_success 'load scheduler' '
	flux module load sched-simple
'

test_done
