#!/bin/sh

test_description='Test resource module'

. `dirname $0`/sharness.sh

# Start out with empty config object
# Then we will reload after adding TOML to cwd
export FLUX_CONF_DIR=$(pwd)

# min SIZE=4
SIZE=$(test_size_large)
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

get_hwloc () {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.get-xml\",nodeid=$1).get_str())"
}
get_topo() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.topo-get\",nodeid=$1).get_str())"
}
res_reload() {
	flux python -c "import flux; print(flux.Flux().rpc(\"resource.reload\",nodeid=$1).get())"
}

test_expect_success 'load resource module with bad option fails' '
	test_must_fail flux module load resource badoption
'

test_expect_success 'load resource module' '
	load_resource
'

test_expect_success HAVE_JQ 'resource.eventlog exists' '
	flux kvs eventlog get -u resource.eventlog >eventlog.out
'

test_expect_success HAVE_JQ 'resource-init context says restart=false' '
	test "$(grep_event resource-init <eventlog.out|jq .restart)" = "false"
'

test_expect_success HAVE_JQ 'resource-init context says online=0' '
	test "$(grep_event resource-init <eventlog.out|jq .online)" = "\"0\""
'

test_expect_success HAVE_JQ 'wait until resource-define event is posted' '
	flux kvs eventlog wait-event -t 5 resource.eventlog resource-define
'

test_expect_success 'resource.R is populated after resource-define' '
	flux kvs get resource.R
'

test_expect_success 'resource.get-xml works on rank 0' '
	get_hwloc 0 >hwloc.json
'

test_expect_success 'resource.get-xml fails on rank 1' '
	test_must_fail get_hwloc 1
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

test_expect_success HAVE_JQ 'new resource-init context says restart=true' '
	test "$(grep_event resource-init <post_restart.out \
		|jq .restart)" = "true"
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

test_expect_success HAVE_JQ 'extract hwloc XML from JSON object' '
	mkdir -p hwloc &&
	for i in $(seq 0 $(($SIZE-1))); do \
		jq -r .xml[$i] hwloc.json >hwloc/$i.xml; \
	done
'

test_expect_success HAVE_JQ 'get hwloc XML direct from ranks' '
	mkdir -p hwloc_direct &&
	for i in $(seq 0 $(($SIZE-1))); do \
		get_topo $i >hwloc_direct/$i.xml || return 1; \
	done
'

test_expect_success HAVE_JQ 'hwloc XML from both sources match' '
	for i in $(seq 0 $(($SIZE-1))); do \
		test_cmp hwloc_direct/$i.xml hwloc/$i.xml || return 1; \
	done
'

test_expect_success 'reloading XML results in same R as before' '
	flux kvs get resource.R >R.orig &&
	flux resource reload -x hwloc &&
	flux kvs get resource.R >R.new &&
	test_cmp R.orig R.new
'

test_expect_success 'reload original R just to do it and verify' '
	flux resource reload R.orig &&
	flux kvs get resource.R >R.new2 &&
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

# Since jq is used in the first bit, HAVE_JQ is required in other bits too
test_expect_success HAVE_JQ 'change expected cores in resource.R' "
	flux kvs get resource.R &&
	jq '.execution.R_lite[0].children.core = \"0-1048\"' R.orig >R.Mcore &&
	flux resource reload R.Mcore
"
test_expect_success HAVE_JQ 'reload resource module' '
	flux exec -r all flux module remove resource &&
	load_resource
'
test_expect_success HAVE_JQ 'all ranks were drained' '
	has_event drain >has_drain2.out &&
	test $(wc -l <has_drain2.out) -eq $(($SIZE+1))
'

test_expect_success 'resource.get-xml blocks until all ranks are up' '
	flux python ${SHARNESS_TEST_SRCDIR}/resource/get-xml-test.py
'

test_expect_success 'unload resource module' '
	flux exec -r all flux module remove resource
'

test_done
