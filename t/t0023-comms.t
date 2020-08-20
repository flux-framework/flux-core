#!/bin/sh

test_description='Test flux-comms utility'

. `dirname $0`/sharness.sh

SIZE=$(test_size_large)
test_under_flux $SIZE minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'flux comms up --wait-for=all works' '
	flux comms up --wait-for=all >idset.out &&
	cat >idset-all.exp <<-EOT &&
	[0-$(($SIZE-1))]
	EOT
	tail -1 <idset.out >idset.out.1 &&
	test_cmp idset-all.exp idset.out.1
'

test_expect_success 'flux comms up with no argument works' '
	flux comms up >idset2.out &&
	test_cmp idset-all.exp idset2.out
'

test_expect_success 'flux comms up --wait-for=all --quiet works' '
	flux comms up --wait-for=all --quiet >idset_quiet.out &&
	test $(wc -l <idset_quiet.out) -eq 0
'

test_expect_success 'flux comms up --wait-for single rank subset works' '
	flux comms up --wait-for=0 --quiet
'

test_expect_success 'flux comms up -h prints usage' '
	flux comms up -h 2>usage.err &&
	grep Usage: usage.err
'

test_expect_success 'flux comms up with extra positional argument fails' '
	test_must_fail flux comms up xyz
'

test_expect_success 'flux comms up with unknown argument fails' '
	test_must_fail flux comms up --unknown
'

test_expect_success 'flux comms up --wait-for fails with bad argument' '
	test_must_fail flux comms up --wait-for=badarg
'

test_expect_success 'flux comms up --wait-for fails with out of range argument' '
	test_must_fail flux comms up --wait-for=0-$SIZE
'

test_expect_success 'state-machine.quorum-monitor RPC directed to rank > 0 fails with EPROTO' '
	flux exec -r 1 $RPC state-machine.quorum-monitor 71 </dev/null
'

test_done
