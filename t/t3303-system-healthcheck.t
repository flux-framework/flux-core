#!/bin/sh
#

test_description='Verify subtree health transitions and tools

Ensure that the overlay subtree health status transitions
appropriately as brokers are taken offline or lost, and also
put flux overlay status tool and related RPCs and subcommands
through their paces.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_TOPO=kary:2

test_under_flux 15 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

overlay_connected_children() {
	rank=$1
        flux python -c "import flux; print(flux.Flux().rpc(\"overlay.stats-get\",nodeid=${rank}).get_str())" | jq -r '.["child-connected"]'
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
	test_must_fail flux overlay status --timeout=0 --summary --rank 99
'

test_expect_success 'flux overlay fails on bad subcommand' '
	test_must_fail flux overlay notcommand
'

test_expect_success 'overlay status is full' '
	test "$(flux overlay status --timeout=0 --summary)" = "full"
'

test_expect_success 'wait timeout of zero is not an immediate timeout' '
	flux overlay status --wait=full --summary --timeout=0
'

test_expect_success 'flux overlay status --highlight option works (color)' '
	flux overlay status --highlight=14 --color=always > highlight.out &&
	grep "\[01;34" highlight.out > highlighted.out &&
	cat <<-EOF >highlighted.expected &&
	[01;34m0 fake0[0m: full
	└─ [01;34m2 fake2[0m: full
	   └─ [01;34m6 fake6[0m: full
	      └─ [01;34m14 fake14[0m: full
	EOF
	test_cmp highlighted.expected highlighted.out
'

test_expect_success 'flux overlay status --highlight option takes hostlist' '
	flux overlay status --highlight=fake[2,6] | grep "<<" > hl2.out &&
	cat <<-EOF >hl2.expected &&
	<<0 fake0>>: full
	└─ <<2 fake2>>: full
	   └─ <<6 fake6>>: full
	EOF
	test_cmp hl2.expected hl2.out
'

test_expect_success 'flux overlay status --highlight expected failures' '
	test_must_fail flux overlay status --highlight=0-16 &&
	test_must_fail flux overlay status --highlight=fake16
'

test_expect_success 'flux overlay status --color option works' '
	test_must_fail flux overlay status -Lfoo &&
	test_must_fail flux overlay status --color=foo &&
	flux overlay status -Lnever --highlight=0 | grep "<<0" &&
	flux overlay status --color=never --highlight=0 | grep "<<0" &&
	flux overlay status -Lauto --highlight=0 | grep "<<0" &&
	flux overlay status --color=auto --highlight=0 | grep "<<0" &&
	flux overlay status -L --highlight=0 | grep "\[" &&
	flux overlay status --color --highlight=0 | grep "\["
'

test_expect_success 'stop broker 3 with children 7,8' '
	$startctl kill 3 15
'

test_expect_success 'wait for rank 0 overlay status to be partial' '
	run_timeout 10 flux overlay status \
		--timeout=0 --rank 0 --summary --wait=partial
'

# Just because rank 0 is partial doesn't mean rank 3 is offline yet
# (shutdown starts at the leaves, and rank 3 will turn partial as
# soon as one of its children goes offline)
test_expect_success 'wait for rank 1 to lose connection with rank 3' '
	wait_connected 1 1 10 0.2
'

test_expect_success 'flux overlay status -vv works' '
	flux overlay status --timeout=0 -vv
'

test_expect_success 'flux overlay status shows rank 3 offline' '
	echo "3 fake3: offline" >health.exp &&
	flux overlay status --timeout=0 --no-pretty \
		| grep fake3 >health.out &&
	test_cmp health.exp health.out
'

test_expect_success 'flux overlay status --summary' '
	flux overlay status --timeout=0 --summary
'

test_expect_success 'flux overlay status --down' '
	flux overlay status --timeout=0 --down
'

test_expect_success 'flux overlay status -vv' '
	flux overlay status --timeout=0 -vv
'

test_expect_success 'flux overlay status: 0,1:partial, 3:offline' '
	flux overlay status --timeout=0 --no-pretty  >health2.out &&
	grep "0 fake0: partial" health2.out &&
	grep "1 fake1: partial" health2.out &&
	grep "3 fake3: offline" health2.out
'

test_expect_success 'flux overlay status: 0-1:partial, 3,7-8:offline' '
	flux overlay status --timeout=0 --no-pretty >health3.out &&
	grep "0 fake0: partial" health3.out &&
	grep "1 fake1: partial" health3.out &&
	grep "3 fake3: offline" health3.out &&
	grep "7 fake7: offline" health3.out &&
	grep "8 fake8: offline" health3.out
'

test_expect_success 'flux overlay status: 0,1:partial, 3,7-8:offline, rest:full' '
	flux overlay status --timeout=0 --no-pretty >health4.out &&
	grep "0 fake0: partial" health4.out &&
	grep "1 fake1: partial" health4.out &&
	grep "3 fake3: offline" health4.out &&
	grep "7 fake7: offline" health4.out &&
	grep "8 fake8: offline" health4.out &&
	grep "4 fake4: full" health4.out &&
	grep "9 fake9: full" health4.out &&
	grep "10 fake10: full" health4.out &&
	grep "2 fake2: full" health4.out &&
	grep "5 fake5: full" health4.out &&
	grep "11 fake11: full" health4.out &&
	grep "6 fake6: full" health4.out &&
	grep "13 fake13: full" health4.out &&
	grep "14 fake14: full" health4.out
'

test_expect_success 'kill broker 14' '
	$startctl kill 14 9
'

# Ensure an EHOSTUNREACH is encountered to trigger connected state change.
test_expect_success 'ping to rank 14 fails with EHOSTUNREACH' '
	echo "flux-ping: 14!broker.ping: $(strerror_symbol EHOSTUNREACH)" >ping.exp &&
	test_must_fail flux ping 14 2>ping.err &&
	test_cmp ping.exp ping.err
'

test_expect_success 'wait for rank 0 subtree to be degraded' '
	run_timeout 10 flux overlay status --timeout=0 --summary --wait=degraded
'

test_expect_success 'wait for unknown status fails' '
	test_must_fail flux overlay status --timeout=0 --wait=foo
'

test_expect_success 'wait timeout works' '
	test_must_fail flux overlay status --wait=full --summary --timeout=0.1s
'

test_expect_success 'flux overlay status -vv' '
	flux overlay status --timeout=0 -vv
'
test_expect_success 'flux overlay status -v' '
	flux overlay status --timeout=0 -v
'

test_expect_success 'flux overlay lookup with no target fails' '
	test_must_fail flux overlay lookup
'

test_expect_success 'flux overlay lookup 0 works' '
	echo fake0 >host.0.exp &&
	flux overlay lookup 0 >host.0.out &&
	test_cmp host.0.exp host.0.out
'

test_expect_success 'flux overlay lookup 0-14 works' '
	echo "fake[0-14]" >host.0-14.exp &&
	flux overlay lookup 0-14 >host.0-14.out &&
	test_cmp host.0-14.exp host.0-14.out
'

test_expect_success 'flux overlay lookup fails on invalid idset target' '
	test_must_fail flux overlay lookup -- -1
'

test_expect_success 'flux overlay lookup fails on too big rank target' '
	test_must_fail flux overlay lookup 100
'

test_expect_success 'flux overlay lookup works on single host target' '
	echo 2 >idset.2.exp &&
	flux overlay lookup fake2 >idset.2.out &&
	test_cmp idset.2.exp idset.2.out
'
test_expect_success 'flux overlay lookup works on multi-host target' '
	echo "2-3" >idset.23.exp &&
	flux overlay lookup "fake[2-3]" >idset.23.out &&
	test_cmp idset.23.exp idset.23.out
'

test_expect_success 'flux overlay lookup fails on invalid hostlist target' '
	test_must_fail flux overlay lookup "fake2["
'
test_expect_success 'flux overlay lookup fails on unknown host target' '
	test_must_fail flux overlay lookup foo
'

test_expect_success 'flux overlay disconnect fails on unknown rank target' '
	test_must_fail flux overlay disconnect 42 2>discon.err &&
	grep "is not a valid rank in this instance" discon.err
'

test_expect_success 'flux overlay disconnect interprets rank w/ extra as host' '
	test_must_fail flux overlay disconnect 42xxx 2>disconn2.err &&
	grep "TARGET must be a valid rank or hostname" disconn2.err
'

test_expect_success 'flux overlay disconnect fails on bad input' '
	test_must_fail flux overlay disconnect "fake2[" 2>disconn3.err &&
	grep "TARGET must be a valid rank or hostname" disconn3.err
'

test_expect_success 'stop broker 12' '
	$startctl kill 12 19
'

test_expect_success 'flux overlay status prints connection timed out on 12' '
	flux overlay status --no-pretty >status.out &&
	grep "fake12: $(strerror_symbol ETIMEDOUT)" status.out
'

test_expect_success 'continue broker 12' '
	$startctl kill 12 18
'

test_done
