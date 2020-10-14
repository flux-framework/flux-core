#!/bin/sh

test_description='Test resource module'

. `dirname $0`/sharness.sh

# Start out with empty config object
# Then we will reload after adding TOML to cwd
export FLUX_CONF_DIR=$(pwd)

# min SIZE=4
SIZE=$(test_size_large)
test_under_flux $SIZE kvs

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
WAITFILE="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

# Usage: grep_event event-name <in >out
grep_event () {
	jq -c ". | select(.name == \"$1\") | .context"
}
has_event() {
	flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}
# Usage: wait_event tries name
wait_event() {
	count=$1
	name=$2
	while ! has_event $name; do
		count=$(($count-1))
		test $count -gt 0 || return 1
		sleep 1
	done
}
# Usage: drain_idset idset reason
drain_idset() {
	local idset=$1; shift
	local reason="$@"
	jq -j -c -n  "{idset:\"$idset\",reason:\"$reason\"}" \
		| $RPC resource.drain
}
# Usage: drain_idset_noreason idset
drain_idset_noreason() {
	local idset=$1
	jq -j -c -n  "{idset:\"$idset\"}" | $RPC resource.drain
}
# Usage: undrain_idset idset
undrain_idset() {
	local idset=$1
	jq -j -c -n  "{idset:\"$idset\"}" | $RPC resource.undrain
}
# Usage: acquire_stream timeout outfile [end-event]
acquire_stream() {
	run_timeout $1 $RPC_STREAM resource.acquire $3 </dev/null >$2
}

test_expect_success HAVE_JQ 'wait_event function fails when it should' '
	! wait_event 1 noexist
'

test_expect_success 'load aggregator module needed for flux hwloc reload' '
	flux exec -r all flux module load aggregator
'

test_expect_success 'load resource module with bad option fails' '
	test_must_fail flux module load resource badoption
'

test_expect_success 'load resource module' '
	for rank in $(seq 0 $(($SIZE-1))); do \
		flux exec -r $rank flux module load resource; \
	done
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

test_expect_success 'reconfigure with rank 0 exclusion' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0"
	EOT
	flux config reload
'

test_expect_success HAVE_JQ 'exclude event was posted with expected idset' '
	flux kvs eventlog get -u resource.eventlog \
		| grep_event exclude >exclude.out &&
	test "$(jq .idset <exclude.out)" = "\"0\""
'

test_expect_success HAVE_JQ 'wait until resource-define event is posted' '
	wait_event 5 resource-define
'

test_expect_success 'resource.R is populated after resource-define' '
	flux kvs get resource.R
'

test_expect_success HAVE_JQ 'drain works with no reason' '
	drain_idset_noreason 1 &&
	flux kvs eventlog get -u resource.eventlog \
		| grep_event drain | tail -1 >drain.out &&
	test $(jq .idset drain.out) = "\"1\""
'

test_expect_success HAVE_JQ 'drain works to update reason' '
	drain_idset 1 reason_01 &&
	flux kvs eventlog get -u resource.eventlog \
		| grep_event drain | tail -1 >drain2.out &&
	test $(jq .reason drain2.out) = "\"reason_01\""
'

test_expect_success HAVE_JQ 'undrain works' '
	undrain_idset 1 &&
	flux kvs eventlog get -u resource.eventlog \
		| grep_event undrain | tail -1 >undrain.out &&
	test $(jq .idset undrain.out) = "\"1\""
'

test_expect_success HAVE_JQ 'undrain fails if rank not drained' '
	test_must_fail undrain_idset 1 2>undrain_not.err &&
	grep "rank 1 not drained" undrain_not.err
'

test_expect_success HAVE_JQ 'drain fails if idset is malformed' '
	test_must_fail drain_idset_noreason xxzz 2>drain_badset.err &&
	grep "failed to decode idset" drain_badset.err
'

test_expect_success HAVE_JQ 'drain fails if idset is empty' '
	test_must_fail drain_idset_noreason "" 2>drain_empty.err &&
	grep "idset is empty" drain_empty.err
'

test_expect_success HAVE_JQ 'drain fails if idset is out of range' '
	test_must_fail drain_idset_noreason "0-$SIZE" 2>drain_range.err &&
	grep "idset is out of range" drain_range.err
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

test_expect_success 'reconfig with bad exclude idset fails' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "xxzz"
	EOT
	test_must_fail flux config reload
'

test_expect_success 'reconfig with out of range exclude idset fails' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0-$SIZE"
	EOT
	test_must_fail flux config reload
'

test_expect_success HAVE_JQ 'reconfig with no exclude idset generates event' '
	cat >resource.toml <<-EOT &&
	[resource]
	EOT
	flux config reload &&
	flux kvs eventlog get -u resource.eventlog >reconfig.out &&
	test "$(grep_event unexclude <reconfig.out | jq .idset)" = "\"0\""
'

test_expect_success HAVE_JQ 'acquire works and response contains up, resources' '
	$RPC resource.acquire </dev/null >acquire.out &&
	jq -c -e -a .resources acquire.out &&
	jq -c -e -a .up acquire.out
'

test_expect_success 'acquire works again after first acquire disconnected' '
	$RPC resource.acquire </dev/null
'

test_expect_success 'reload config/module excluding rank 0' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0"
	EOT
	flux config reload &&
	flux module reload resource monitor-force-up
'

test_expect_success HAVE_JQ 'acquire returns resources excluding rank 0' '
	$RPC resource.acquire </dev/null >acquire2.out &&
	jq -e -r .resources.execution.R_lite[0].rank acquire2.out \
		>acquire2_rank.out &&
	echo "1-$(($SIZE-1))" >acquire2_rank.exp &&
	test_cmp acquire2_rank.exp acquire2_rank.out
'

test_expect_success HAVE_JQ 'acquire returns up excluding rank 0' '
	jq -c -e -a .up acquire2.out >acquire2_up.out &&
	grep "^\"1-$(($SIZE-1))" acquire2_up.out
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'drain rank 1 causes down response' '
	acquire_stream 30 acquire3.out down &
	pid=$! &&
	$WAITFILE -t 10 -v -p \"resources\" acquire3.out &&
	drain_idset_noreason "1" &&
	wait $pid
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'undrain/drain rank 1 causes up,down responses' '
	acquire_stream 30 acquire4.out down &
	pid=$! &&
	$WAITFILE -t 10 -v -p \"resources\" acquire4.out &&
	undrain_idset "1" &&
	$WAITFILE -t 10 -v -p \"up\" -c 2 acquire4.out &&
	drain_idset "1" &&
	wait $pid
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'add/remove new exclusion causes down/up response' '
	acquire_stream 30 acquire5.out &
	pid=$! &&
	$WAITFILE -t 10 -v -p \"resources\" acquire5.out &&
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0,3"
	EOT
	flux config reload &&
	$WAITFILE -t 10 -v -p \"down\" acquire5.out &&
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0"
	EOT
	flux config reload &&
	$WAITFILE -t 10 -v -p \"up\" -c 2 acquire5.out &&
	kill -15 $pid && wait $pid || true
'

test_expect_success 'unload resource module (rank 0)' '
	flux module remove resource
'
test_expect_success 'clear eventlog' '
	flux kvs unlink resource.eventlog
'
test_expect_success 'unload resource module (rank 2)' '
	flux exec -r 2 flux module remove resource
'
test_expect_success 'load resource module (rank 0) with monitor-force-up' '
	flux module load resource monitor-force-up
'
test_expect_success HAVE_JQ 'resource-init context says online=<all>' '
	wait_event 5 resource-init &&
	flux kvs eventlog get -u resource.eventlog >ev2.out &&
	echo "\"0-$((${SIZE}-1))\"" >ev2_online.exp &&
	grep_event resource-init <ev2.out|jq .online >ev2_online.out &&
	test_cmp ev2_online.exp ev2_online.out
'
test_expect_success HAVE_JQ 'no online events posted due to monitor-force-up' '
	! wait_event 1 online
'

test_expect_success 'load-then-unload resource module (rank 2)' '
	flux exec -r2 flux module load resource &&
	flux exec -r2 flux module remove resource
'

test_expect_success HAVE_JQ 'offline event posted for rank 2' '
	wait_event 5 offline &&
	flux kvs eventlog get -u resource.eventlog >ev3.out &&
	grep_event offline <ev3.out|jq .idset | grep -x "\"2\""
'

test_expect_success 'unload resource module (except rank 2)' '
	flux exec -r all -x 2 flux module remove resource
'
test_expect_success 'unload aggregator module' '
	flux exec -r all flux module remove aggregator
'

test_done
