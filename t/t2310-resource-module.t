#!/bin/sh

test_description='Test resource module'

. `dirname $0`/sharness.sh

# Start out with empty config object
# Then we will reload after adding TOML to cwd
export FLUX_CONF_DIR=$(pwd)

SIZE=4
test_under_flux $SIZE kvs

# Usage: grep_event event-name <in >out
grep_event () {
	jq -c ". | select(.name == \"$1\") | .context"
}
has_event() {
	flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}
# Ensure that module is loaded upstream-to-downstream TBON order
load_resource () {
	for rank in $(seq 0 $(($SIZE-1))); do \
		flux exec -r $rank flux module load resource; \
	done
}

get_topo() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.topo-get\",nodeid=$1).get_str())"
}
res_reload() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.reload\",nodeid=$1).get())"
}
bad_reduce() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.topo-reduce\",nodeid=$1))"
}

test_expect_success 'load resource module with bad option fails' '
	test_must_fail flux module load resource badoption
'

#   0
#  1 2
# 3

test_expect_success 'load resource module on ranks 0,2' '
	flux module load resource &&
	flux exec -r2 flux module load resource
'
test_expect_success 'reload module on rank 2 to trigger dup topo-reduce' '
	flux exec -r2 flux module reload resource
'
test_expect_success 'load resource module on ranks 1,3' '
	flux exec -r1 flux module load resource &&
	flux exec -r3 flux module load resource
'
test_expect_success 'send bad topo-reduce request to rank 0' '
	bad_reduce 0
'

test_expect_success 'resource.eventlog exists' '
	flux kvs eventlog get -u resource.eventlog >eventlog.out
'

test_expect_success 'wait until resource-define event is posted' '
	flux kvs eventlog wait-event -t 5 resource.eventlog resource-define
'

test_expect_success 'resource.R is populated after resource-define' '
	flux kvs get resource.R
'

test_expect_success 'reload resource module and re-capture eventlog' '
	flux module remove resource &&
	flux kvs eventlog get -u resource.eventlog >pre_restart.out &&
	flux module load resource &&
	flux kvs eventlog get -u resource.eventlog >restart.out &&
	pre=$(wc -l <pre_restart.out) &&
	post=$(wc -l <restart.out) &&
	tail -$(($post-$pre)) restart.out > post_restart.out
'

test_expect_success 'reconfig with extra key fails' '
	cat >resource.toml <<-EOT &&
	[resource]
	foo = 42
	EOT
	test_must_fail flux config reload
'

test_expect_success 'reconfig with empty config' '
	rm -f resource.toml &&
	flux config reload
'

test_expect_success 'flux resource reload fails on rank 1' '
	test_must_fail res_reload 1
'

test_expect_success 'flux resource reload fails on nonexistent file' '
	test_must_fail flux resource reload /noexist
'

test_expect_success 'flux resource reload fails on nonexistent XML directory' '
	test_must_fail flux resource reload -x /noexist
'

test_expect_success 'flux resource reload fails on empty XML directory' '
	mkdir empty &&
	test_must_fail flux resource reload -x $(pwd)/empty
'

sanitize_hwloc_xml() {
    sed 's/pci_link_speed=".*"//g' $1
}

test_expect_success 'get hwloc XML direct from ranks' '
	mkdir -p hwloc &&
	for i in $(seq 0 $(($SIZE-1))); do \
		get_topo $i | sanitize_hwloc_xml >hwloc/$i.xml || return 1; \
	done
'

normalize_json() {
	jq -cS .
}

test_expect_success 'reloading XML results in same R as before' '
	flux kvs get resource.R | normalize_json >R.orig &&
	flux resource reload -x hwloc &&
	flux kvs get resource.R | normalize_json >R.new &&
	test_cmp R.orig R.new
'

test_expect_success 'reload original R just to do it and verify' '
	flux resource reload R.orig &&
	flux kvs get resource.R | normalize_json >R.new2 &&
	test_cmp R.orig R.new2
'

test_expect_success 'reload refuses to load XML beyond size' '
	cp hwloc/$(($SIZE-1)).xml hwloc/$SIZE.xml &&
	test_must_fail flux resource reload -x hwloc 2>toobig.err &&
	grep "contains ranks execeeding size" toobig.err
'

test_expect_success 'the --force option makes it work' '
	flux resource reload -f -x hwloc
'

test_expect_success 'reload allows loading XML less than size' '
	rm -f hwloc/$SIZE.xml &&
	rm -f hwloc/$(($SIZE-1)).xml &&
	flux resource reload -x hwloc
'

test_expect_success 'reload resource module' '
	flux exec -r all flux module remove resource &&
	load_resource
'

test_expect_success 'one rank was was drained' '
	has_event drain >has_drain.out &&
	test $(wc -l <has_drain.out) -eq 1
'

# Since jq is used in the first bit, is required in other bits too
test_expect_success 'change expected cores in resource.R' "
	flux kvs get resource.R &&
	jq '.execution.R_lite[0].children.core = \"0-1048\"' R.orig >R.Mcore &&
	flux resource reload R.Mcore
"
test_expect_success 'reload resource module' '
	flux exec -r all flux module remove resource &&
	load_resource
'
test_expect_success 'all ranks were drained' '
	has_event drain >has_drain2.out &&
	test $(wc -l <has_drain2.out) -eq $(($SIZE+1))
'

test_expect_success 'flux resource status works on rank > 0' '
	flux exec -r 1 flux resource status
'

status_onrank() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.status\",nodeid=$1).get())"
}

test_expect_success 'resource.status RPC fails on rank > 0' '
	test_must_fail status_onrank 1 2>status.err &&
	grep "only works on rank 0" status.err
'

test_expect_success 'unload resource module' '
	flux exec -r all flux module remove resource
'

test_expect_success 'reconfig with bad R path' '
	cat >resource.toml <<-EOT &&
	[resource]
	path = "/noexist"
	EOT
	flux config reload
'

test_expect_success 'load resource module fails due to bad R' '
	test_must_fail flux module load resource
'

test_done
