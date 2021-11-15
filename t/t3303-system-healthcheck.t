#!/bin/sh
#

test_description='Verify subtree health transitions and tools

Ensure that the overlay subtree health status transitions
appropriately as brokers are taken offline or lost, and also
put flux overlay status tool and related RPCs and subcommands
through their paces.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_FANOUT=2

test_under_flux 15 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

overlay_connected_children() {
	rank=$1
        flux python -c "import flux; print(flux.Flux().rpc(\"overlay.stats.get\",nodeid=${rank}).get_str())" | jq -r '.["child-connected"]'
}

# Usage: wait_connected rank count tries delay
wait_connected() {
	local rank=$1
	local count=$2
	local tries=$3
	local delay=$4

	while test $tries -gt 0; do
		local n=$(overlay_connected_children $rank)
		echo $n children
		test $n -eq $count && return 0
		sleep $delay
		tries=$(($tries-1))
	done
	return 1
}

bad_topo_request() {
        flux python -c "import flux; print(flux.Flux().rpc(\"overlay.topology\",nodeid=0).get_str())"
}
bad_topo_request_rank99() {
        flux python -c "import flux; print(flux.Flux().rpc(\"overlay.topology\",{\"rank\":99},nodeid=0).get_str())"
}

test_expect_success 'overlay.topology RPC with no payload fails' '
	test_must_fail bad_topo_request
'
test_expect_success 'overlay.topology RPC with bad rank fails' '
	test_must_fail bad_topo_request_rank99
'

test_expect_success 'flux overlay status fails on bad rank' '
	test_must_fail flux overlay status --rank 99
'

test_expect_success 'flux overlay fails on bad subcommand' '
	test_must_fail flux overlay notcommand
'

test_expect_success 'flux overlay status --hostnames works on PMI instance' '
	flux start flux overlay status -vvv --hostnames
'

test_expect_success 'flux overlay status --hostnames fails on PMI instance without R' '
	test_must_fail flux start \
		"flux kvs get --waitcreate resource.R && \
		flux kvs unlink resource.R && \
		flux overlay status -vvv --hostnames"
'

test_expect_success 'flux overlay status --hostnames fails on bad hostlist' '
	test_must_fail flux start -o,-Sbroker.hostlist="[-badlist" \
		flux overlay status -vvv --hostnames
'

test_expect_success 'overlay status is full' '
	test "$(flux overlay status)" = "full"
'

test_expect_success 'flux overlay status -v prints full' '
	echo full >health.exp &&
	flux overlay status -v >health.out &&
	test_cmp health.exp health.out
'

test_expect_success 'stop broker 3 with children 7,8' '
	$startctl kill 3 15
'

test_expect_success 'wait for rank 0 overlay status to be partial' '
	flux overlay status --rank 0 --wait=partial --timeout=10s
'

# Just because rank 0 is partial doesn't mean rank 3 is offline yet
# (shutdown starts at the leaves, and rank 3 will turn partial as
# soon as one of its children goes offline)
test_expect_success HAVE_JQ 'wait for rank 1 to lose connection with rank 3' '
	wait_connected 1 1 10 0.2
'

test_expect_success 'flux overlay status -vv and gaudy options works' '
	flux overlay status -vv --pretty --times --hostnames --ghost
'

test_expect_success 'flux overlay status -v shows rank 3 offline' '
	echo "3: offline" >health_v.exp &&
	flux overlay status -v >health_v.out &&
	test_cmp health_v.exp health_v.out
'

test_expect_success 'flux overlay status -v --ghost --color' '
	flux overlay status -v --ghost --color
'

test_expect_success 'flux overlay status -vv --ghost --color' '
	flux overlay status -vv --ghost --color
'

test_expect_success 'flux overlay status -vvv --ghost --color' '
	flux overlay status -vvv --ghost --color
'

test_expect_success 'flux overlay status -vv: 0,1:partial, 3:offline' '
	cat >health_vv.exp <<-EOT &&
	0: partial
	1: partial
	3: offline
	EOT
	flux overlay status -vv >health_vv.out &&
	test_cmp health_vv.exp health_vv.out
'

test_expect_success 'flux overlay status -vvg: 0-1:partial, 3,7-8:offline' '
	cat >health_vvg.exp <<-EOT &&
	0: partial
	1: partial
	3: offline
	7: offline
	8: offline
	EOT
	flux overlay status -vvg >health_vvg.out &&
	test_cmp health_vvg.exp health_vvg.out
'

test_expect_success 'flux overlay status -vvvg: 0,1:partial, 3,7-8:offline, rest:full' '
	cat >health_vvvg.exp <<-EOT &&
	0: partial
	1: partial
	3: offline
	7: offline
	8: offline
	4: full
	9: full
	10: full
	2: full
	5: full
	11: full
	12: full
	6: full
	13: full
	14: full
	EOT
	flux overlay status -vvvg >health_vvvg.out &&
	test_cmp health_vvvg.exp health_vvvg.out
'

test_expect_success 'kill broker 14' '
	$startctl kill 14 9
'

# Ensure an EHOSTUNREACH is encountered to trigger connected state change.
test_expect_success 'ping to rank 14 fails with EHOSTUNREACH' '
	echo "flux-ping: 14!broker.ping: No route to host" >ping.exp &&
	test_must_fail flux ping 14 2>ping.err &&
	test_cmp ping.exp ping.err
'

test_expect_success 'wait for rank 0 subtree to be degraded' '
	flux overlay status --wait=degraded --timeout=10s
'

test_expect_success 'wait for unknown status fails' '
	test_must_fail flux overlay status --wait=foo
'

test_expect_success 'wait timeout works' '
	test_must_fail flux overlay status --wait=full --timeout=0.1s
'

test_expect_success 'flux overlay status -vg --since' '
	flux overlay status -vg --since
'
test_expect_success 'flux overlay status -vvgpc' '
	flux overlay status -vvgpc
'
test_expect_success 'flux overlay status -vvvgpc' '
	flux overlay status -vvv --ghost --pretty --color
'

test_expect_success 'flux overlay gethostbyrank with no rank fails' '
	test_must_fail flux overlay gethostbyrank
'

test_expect_success 'flux overlay gethostbyrank 0 works' '
	echo fake0 >host.0.exp &&
	flux overlay gethostbyrank 0 >host.0.out &&
	test_cmp host.0.exp host.0.out
'

test_expect_success 'flux overlay gethostbyrank 0-14 works' '
	echo "fake[0-14]" >host.0-14.exp &&
	flux overlay gethostbyrank 0-14 >host.0-14.out &&
	test_cmp host.0-14.exp host.0-14.out
'

test_expect_success 'flux overlay gethostbyrank fails on invalid idset' '
	test_must_fail flux overlay gethostbyrank -- -1
'

test_expect_success 'flux overlay gethostbyrank fails on out of range rank' '
	test_must_fail flux overlay gethostbyrank 100
'

test_done
