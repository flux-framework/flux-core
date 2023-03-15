#!/bin/sh

test_description='Test resource acquire'

. `dirname $0`/sharness.sh

# Start out with empty config object
# Then we will reload after adding TOML to cwd
export FLUX_CONF_DIR=$(pwd)

SIZE=4
test_under_flux $SIZE

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
WAITFILE="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
IDSETUTIL=${FLUX_BUILD_DIR}/src/common/libidset/test_idsetutil

# Usage: acquire_stream timeout outfile [end-event]
acquire_stream() {
	run_timeout $1 $RPC_STREAM resource.acquire $3 </dev/null >$2
}

# Usage: waitup N
#   where N is a count of online ranks
waitup () {
	run_timeout 5 flux python -c "import flux; print(flux.Flux().rpc(\"resource.monitor-waitup\",{\"up\":$1}).get())"
}
waitdown () {
	waitup $(($SIZE-$1))
}

test_expect_success 'wait for monitor to declare all ranks are up' '
	waitdown 0
'

test_expect_success 'unload scheduler' '
	flux module remove sched-simple
'

test_expect_success 'acquire works and response contains up, resources' '
	$RPC resource.acquire </dev/null >acquire.out &&
	jq -c -e -a .resources acquire.out &&
	jq -c -e -a .up acquire.out
'

test_expect_success 'acquire works again after first acquire disconnected' '
	$RPC resource.acquire </dev/null
'

test_expect_success 'reload config excluding rank 0' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "0"
	EOT
	flux config reload
'

test_expect_success 'acquire returns resources excluding rank 0' '
	$RPC resource.acquire </dev/null >acquire2.out &&
	jq -r .resources.execution.R_lite[0].rank acquire2.out \
		>acquire2_rank.out &&
	echo "1-$(($SIZE-1))" >acquire2_rank.exp &&
	test_cmp acquire2_rank.exp acquire2_rank.out
'

test_expect_success 'acquire returns up excluding rank 0' '
	jq -e -r .up acquire2.out | $IDSETUTIL expand >acquire2_up.out &&
	test_must_fail grep "^0" acquire2_up.out
'

test_expect_success NO_CHAIN_LINT 'drain rank 1 causes down response' '
	acquire_stream 30 acquire3.out down &
	pid=$! &&
	$WAITFILE -t 10 -v -p \"resources\" acquire3.out &&
	flux resource drain "1" &&
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'undrain/drain rank 1 causes up,down responses' '
	acquire_stream 30 acquire4.out down &
	pid=$! &&
	$WAITFILE -t 10 -v -p \"resources\" acquire4.out &&
	flux resource undrain 1 &&
	$WAITFILE -t 10 -v -p \"up\" -c 2 acquire4.out &&
	flux resource drain 1 &&
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'add/remove new exclusion causes down/up response' '
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

test_expect_success 'load scheduler' '
	flux module load sched-simple
'

test_expect_success 'flux resource reload fails with acquire client' '
	flux kvs get resource.R >R &&
	test_must_fail flux resource reload R
'

test_done
