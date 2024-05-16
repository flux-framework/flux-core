#!/bin/sh
#

test_description='Test instance size override'

. `dirname $0`/sharness.sh

test_expect_success 'size override fails if too small' '
	test_must_fail flux broker \
	    -Sbroker.rc1_path= -Sbroker.rc3_path= \
	    -Ssize=1,-Sbroker.quorum=1 /bin/true 2>toosmall.err &&
	grep "may only be increased" toosmall.err
'
test_expect_success 'an instance can be started with an extra rank' '
	flux broker \
	    -Sbroker.rc1_path= -Sbroker.rc3_path= \
	    -Ssize=2 -Sbroker.quorum=1 \
	    flux overlay status --no-pretty >overlay.out
'
test_expect_success 'flux overlay status shows the extra rank offline' '
	grep "1 extra0: offline" overlay.out
'
test_expect_success 'an instance can be started with PMI plus extra' '
	flux start -s2 -o,-Stbon.topo=kary:0 \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
	    -o,-Ssize=3,-Sbroker.quorum=2 \
	    flux overlay status --no-pretty >overlay2.out
'
test_expect_success 'flux overlay status shows the extra rank offline' '
	grep "2 extra0: offline" overlay2.out
'
test_expect_success 'create config with fake resources' '
	cat >fake.toml <<-EOT
	[resource]
	noverify = true
	[[resource.config]]
	hosts = "a,b,c"
	cores = "0"
	EOT
'
test_expect_success 'start full 2-broker instance with one extra rank' '
	flux start -s2 -o,-Ssize=3,-Sbroker.quorum=2,-cfake.toml \
	    flux resource status -s offline -no {nnodes} >offline.out
'
test_expect_success 'flux resource status reports one node offline' '
	cat >offline.exp <<-EOT &&
	1
	EOT
	test_cmp offline.exp offline.out
'

test_done
