#!/bin/sh

test_description='flux-resource status general tests'

. $(dirname $0)/sharness.sh

test_under_flux 4

export FLUX_PYCLI_LOGLEVEL=10

test_expect_success 'flux-resource status: works' '
	flux resource status
'
test_expect_success 'flux-resource status: --states=help reports valid states' '
	flux resource status --states=help >status-help.out 2>&1 &&
	test_debug "cat status-help.out" &&
	grep "valid states:" status-help.out
'
test_expect_success 'flux-resource status: bad --states reports valid states' '
	test_expect_code 1 flux resource status --states=foo >status-bad.out 2>&1 &&
	test_debug "cat status-bad.out" &&
	grep "valid states:" status-bad.out
'
test_expect_success 'flux-resource status -s all shows all expected states' '
	flux resource status -s all >status-all.out &&
	test_debug "cat status-all.out" &&
	grep avail status-all.out &&
	grep exclude status-all.out &&
	grep torpid status-all.out &&
	grep allocated status-all.out &&
	grep draining status-all.out &&
	grep drained status-all.out
'
test_expect_success 'flux-resource status: allocated set is not displayed by default' '
	flux submit -N1 --wait-event=alloc sleep inf &&
	flux resource status >status-noalloc.out &&
	test_must_fail grep allocated status-noalloc.out
'
test_expect_success 'flux-resource status: allocated nodes can be displayed' '
	flux resource status -s allocated | grep allocated
'
test_expect_success 'cancel running jobs' '
	flux cancel --all &&
	flux queue idle
'
# issue#6625:
test_expect_success 'flux-resource status does not combine dissimilar drain reasons' '
	test_when_finished "flux resource undrain -f 0-3" &&
	flux resource drain -f 0-3 xxxxxxxx &&
	flux resource drain -f 0   xxxxxxxxyy &&
	flux resource status -no "{state:>12} {ranks:>6} +:{reason:<5.5+}" \
		> drain.1 &&
	flux resource status -no "{state:>12} {ranks:>6} {reason:<10}" \
		> drain.2 &&
	test_cmp drain.1 drain.2
'
test_expect_success 'flux-resource status shows housekeeping by default' '
	flux config load <<-EOF &&
	[job-manager.housekeeping]
	release-after = "0"
	command = [ "sleep", "inf" ]
	EOF
	flux run -N4 hostname &&
	test_debug "flux housekeeping list" &&
	test_debug "flux resource status" &&
	flux resource status | grep housekeeping &&
	test $(flux resource status -s housekeeping -no {nnodes}) -eq 4
'
test_expect_success 'flux-resource status works after partial release' '
	flux housekeeping kill --targets=0-1 &&
	test_debug "flux housekeeping list" &&
	test_debug "flux resource status" &&
	flux resource status | grep housekeeping &&
	test $(flux resource status -s housekeeping -no {ranks}) = "2-3"
'
test_expect_success 'stop housekeeping tasks' '
	flux housekeeping kill --all
'
get_jm_allocated_nnodes() {
	flux python -c "
import flux
from flux.resource import ResourceSet
r = flux.Flux().rpc('job-manager.resource-status').get()
print(ResourceSet(r['allocated']).nnodes)
"
}
test_expect_success 'job-manager resource-status cache: configure housekeeping' '
	flux config load <<-EOF
	[job-manager.housekeeping]
	release-after = "0"
	command = [ "sleep", "inf" ]
	EOF
'
test_expect_success 'job-manager resource-status cache: populate with empty set' '
	test $(get_jm_allocated_nnodes) -eq 0
'
test_expect_success 'job-manager resource-status cache: correct while job is in housekeeping' '
	flux run -N1 hostname &&
	test_debug "flux housekeeping list" &&
	test $(get_jm_allocated_nnodes) -eq 1 &&
	test $(get_jm_allocated_nnodes) -eq 1
'
test_expect_success 'job-manager resource-status cache: invalidated when housekeeping ends' '
	flux housekeeping kill --all &&
	test $(get_jm_allocated_nnodes) -eq 0 &&
	test $(get_jm_allocated_nnodes) -eq 0
'
# issue#7465:
get_resource_status() {
        flux python -c "import flux; import json; print(json.dumps(flux.Flux().rpc(\"resource.status\").get()))"
}
test_expect_success 'add the optional Rv1 scheduling key to R' "
	flux module unload sched-simple &&
	flux kvs get resource.R | jq  '.scheduling = {foo:42}' >R_scheduling &&
	flux resource reload R_scheduling &&
	flux module load sched-simple &&
	flux kvs get resource.R | jq -e '.scheduling'
"
test_expect_success 'resource.status RPC filters out R.scheduling' "
	get_resource_status | jq -e '.R' >R.out &&
	test_must_fail jq -e '.scheduling' R.out
"

test_done
