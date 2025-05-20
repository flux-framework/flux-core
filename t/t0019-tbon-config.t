#!/bin/sh
#

test_description='Test the brokers tbon.* configuration'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

test_expect_success 'flux-start with size 2 works with tbon.zmqdebug' '
	flux start ${ARGS} -Stbon.zmqdebug=1 -s2 true
'
test_expect_success 'flux-start with non-integer tbon.zmqdebug fails' '
	test_must_fail flux start ${ARGS} -Stbon.zmqdebug=foo true
'
test_expect_success 'tbon.endpoint can be read' '
	ATTR_VAL=`flux start ${ARGS} -s2 flux getattr tbon.endpoint` &&
	echo $ATTR_VAL | grep "://"
'
test_expect_success 'tbon.endpoint uses ipc:// in standalone instance' '
	flux start ${ARGS} -s2 \
		flux getattr tbon.endpoint >endpoint.out &&
	grep "^ipc://" endpoint.out
'
test_expect_success 'tbon.endpoint uses tcp:// if process mapping unavailable' '
	flux start ${ARGS} -s2 --test-pmi-clique=none \
		flux getattr tbon.endpoint >endpoint2.out &&
	grep "^tcp" endpoint2.out
'
test_expect_success 'tcp:// is used with per-broker mapping' '
	flux start ${ARGS} -s2 --test-pmi-clique=per-broker \
		flux exec -r 1 flux getattr tbon.parent-endpoint \
		>endpoint2a.out &&
	grep "^tcp" endpoint2a.out
'
test_expect_success 'tbon.endpoint uses tcp:// if tbon.prefertcp is set' '
	flux start ${ARGS} -s2 -Stbon.prefertcp=1 \
		flux getattr tbon.endpoint >endpoint2.out &&
	grep "^tcp" endpoint2.out
'
test_expect_success 'tbon.endpoint uses ipc and tcp with custom taskmap' '
	cat >endpoints.exp <<-EOT &&
	1: ipc://
	2: ipc://
	3: ipc://
	4: ipc://
	5: tcp://
	6: tcp://
	7: tcp://
	EOT
	flux start ${ARGS} -s8 --test-pmi-clique="[[0,1,5,1],[1,3,1,1]]" \
		flux exec -x 0 --label-io \
		flux getattr tbon.parent-endpoint \
		| sort | cut -c1-9 >endpoints.out &&
		test_cmp endpoints.exp endpoints.out
'
test_expect_success 'FLUX_IPADDR_INTERFACE=lo works' '
	FLUX_IPADDR_INTERFACE=lo flux start \
		${ARGS} -s2 -Stbon.prefertcp=1 \
		flux getattr tbon.endpoint >endpoint3.out &&
	grep "127.0.0.1" endpoint3.out
'
test_expect_success 'tbon.interface-hint=lo works' '
	flux start -Stbon.interface-hint=lo \
		${ARGS} -s2 -Stbon.prefertcp=1 \
		flux getattr tbon.endpoint >endpoint3a.out &&
	grep "127.0.0.1" endpoint3a.out
'
test_expect_success 'tbon.interface-hint=127.0.0.0/8 works' '
	flux start -Stbon.interface-hint=127.0.0.0/8 \
		${ARGS} -s2 -Stbon.prefertcp=1 \
		flux getattr tbon.endpoint >endpoint3b.out &&
	grep "127.0.0.1" endpoint3b.out
'
test_expect_success 'TOML tbon.interface-hint=127.0.0.0/8 works' '
	cat >hint.toml <<-EOT &&
	tbon.interface-hint = "127.0.0.0/8"
	EOT
	flux start --config-path=hint.toml \
		${ARGS} -s2 -Stbon.prefertcp=1 \
		flux getattr tbon.endpoint >endpoint3c.out &&
	grep "127.0.0.1" endpoint3c.out
'
test_expect_success 'TOML tbon.interface-hint=wrong type fails' '
	cat >badhint.toml <<-EOT &&
	tbon.interface-hint = 42
	EOT
	test_must_fail flux start --config-path=badhint.toml ${ARGS} true
'
test_expect_success 'tbon.interface-hint=badiface fails' '
	test_must_fail_or_be_terminated flux start \
		-Stbon.interface-hint=badiface \
		${ARGS} -s2 -Stbon.prefertcp=1 true
'
test_expect_success 'tbon.interface-hint=default-route works' '
	flux start -Stbon.interface-hint=default-route -Stbon.prefertcp=1 \
		${ARGS} -s2 true
'
# Note: the following test may fail if for some reason bind() fails on the
# interface associated with the hostname. This may occur, for example, under
# docker with --network=host when the host does not allow processes to bind
# to the shared interface. Therefore allow success or a specific error to
# determine success of this test.
test_expect_success 'tbon.interface-hint=hostname works' '
	test_might_fail flux start \
		-Stbon.interface-hint=hostname \
		-Stbon.prefertcp=1 \
		${ARGS} -s2 echo ok >interface-hostname.out 2>&1 &&
	(grep ok interface-hostname.out ||
	 grep "Cannot assign requested address" interface-hostname.out)
'
test_expect_success 'tbon.interface-hint defaults to default-router' '
	flux start ${ARGS} flux getattr tbon.interface-hint >defhint.out &&
	grep default-route defhint.out
'
test_expect_success 'tbon.interface-hint default comes from parent' '
	flux start -Stbon.interface-hint=hostname \
	    flux alloc -N1 flux getattr tbon.interface-hint >childhint.out &&
	grep hostname childhint.out
'
test_expect_success 'tbon.interface-hint from parent can be overridden' '
	flux start -Stbon.interface-hint=hostname \
	    flux alloc -N1 --broker-opts=-Stbon.interface-hint=default-router \
	    flux getattr tbon.interface-hint >childhint2.out &&
	grep default-router childhint2.out
'
# Note: the following test has been observed to fail (as expected) in more
# ways than just nonzero exit or terminated by SIGKILL/SIGTERM (in CI the
# test sometimes fails with SIGPIPE). Therefore, use the blanket `!` here
# since we just want to test failure, we don't care how it fails.
test_expect_success 'tbon.endpoint cannot be set' '
	! flux start ${ARGS} -s2 \
		--setattr=tbon.endpoint=ipc:///tmp/customflux true
'
test_expect_success 'tbon.parent-endpoint cannot be read on rank 0' '
	test_must_fail flux start ${ARGS} -s2 flux getattr tbon.parent-endpoint
'
test_expect_success 'tbon.parent-endpoint can be read on not rank 0' '
	NUM=`flux start ${ARGS} -s4 flux exec -n flux getattr tbon.parent-endpoint | grep ipc | wc -l` &&
	test $NUM -eq 3
'
test_expect_success 'broker -Stbon.fanout=4 is an alias for tbon.topo=kary:4' '
	echo kary:4 >fanout.exp &&
	flux start ${ARGS} -Stbon.fanout=4 \
		flux getattr tbon.topo >fanout.out &&
	test_cmp fanout.exp fanout.out
'
test_expect_success 'broker -Stbon.topo=kary:8 option works' '
	echo kary:8 >topo.exp &&
	flux start ${ARGS} -Stbon.topo=kary:8 \
		flux getattr tbon.topo >topo.out &&
	test_cmp topo.exp topo.out
'
test_expect_success 'broker -Stbon.topo=kary:0 works' '
	flux start ${ARGS} -Stbon.topo=kary:0 true
'
test_expect_success 'broker -Stbon.topo=custom option works' '
	echo custom >topo2.exp &&
	flux start ${ARGS} -Stbon.topo=custom \
		flux getattr tbon.topo >topo2.out &&
	test_cmp topo2.exp topo2.out
'
test_expect_success 'broker -Stbon.topo=binomial option works' '
	echo binomial >topo_binomial.exp &&
	flux start ${ARGS} -Stbon.topo=binomial \
		flux getattr tbon.topo >topo_binomial.out &&
	test_cmp topo_binomial.exp topo_binomial.out
'
test_done
