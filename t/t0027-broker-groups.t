#!/bin/sh
#

test_description='Test broker groups'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

GROUPSCMD="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

test_expect_success 'broker.online contains full instance' '
	cat >broker.online.exp <<-EOT &&
	0-3
	EOT
	${GROUPSCMD} get broker.online >broker.online.out &&
	test_cmp broker.online.exp broker.online.out
'

test_expect_success 'flux module stats groups agrees' '
	flux module stats groups >stats.out &&
	jq -e -r .subtree.\"broker.online\" <stats.out >broker.online.stats &&
	test_cmp broker.online.exp broker.online.stats
'
test_expect_success 'groups.get of nonexistent group returns empty set' '
	cat >newgroup.exp <<-EOT &&

	EOT
	${GROUPSCMD} get newgroup >newgroup.out &&
	test_cmp newgroup.exp newgroup.out
'

test_expect_success 'groups.get on rank > 0 fails with reasonable error' '
	test_must_fail ${GROUPSCMD} get --rank 1 broker.online 2>test0.err &&
	grep "only available on rank 0" test0.err
'
test_expect_success 'but flux module stats groups does work' '
	flux exec -r 1 flux module stats groups
'

test_expect_success 'nonlocal groups.join fails with appropriate error' '
	test_must_fail ${GROUPSCMD} join --rank 1 foo 2>rmtjoin.err &&
	grep "restricted to the local broker" rmtjoin.err
'
test_expect_success 'nonlocal groups.leave fails with appropriate error' '
	test_must_fail ${GROUPSCMD} leave --rank 1 foo 2>rmtleave.err &&
	grep "restricted to the local broker" rmtleave.err
'

badjoin() {
	flux python -c "import flux; print(flux.Flux().rpc(\"groups.join\").get())"
}
test_expect_success 'groups.join with malformed payload fails with EPROTO' '
	test_must_fail badjoin 2>badjoin.err &&
	grep "Protocol error" badjoin.err
'

badleave() {
	flux python -c "import flux; print(flux.Flux().rpc(\"groups.leave\").get())"
}
test_expect_success 'groups.leave with malformed payload fails with EPROTO' '
	test_must_fail badleave 2>badleave.err &&
	grep "Protocol error" badleave.err
'

badget() {
	flux python -c "import flux; print(flux.Flux().rpc(\"groups.get\").get())"
}
test_expect_success 'groups.get with malformed payload fails with EPROTO' '
	test_must_fail badget 2>badget.err &&
	grep "Protocol error" badget.err
'

badupdate() {
	flux python -c "import flux; print(flux.Flux().rpc(\"groups.update\"))"
}
test_expect_success 'send groups.update with malformed payload (no response)' '
	badupdate
'

badupdate2() {
	flux python -c "import flux; print(flux.Flux().rpc(\"groups.update\",{\"update\":{\"foo\":42}}))"
}
test_expect_success 'send groups.update with malformed ops array (no response)' '
	badupdate2
'

test_expect_success 'join group and explicitly leave works' '
	${GROUPSCMD} join --leave test1 &&
	${GROUPSCMD} join --leave test1
'

test_expect_success 'join group and disconnect works' '
	${GROUPSCMD} join test2 &&
	${GROUPSCMD} join test2
'

test_expect_success 'join group twice fails with reasonable error' '
	test_must_fail ${GROUPSCMD} join --dubjoin test3 2>test3.err &&
	grep "already a member" test3.err
'

test_expect_success 'leave group twice fails with reasonable error' '
	test_must_fail ${GROUPSCMD} join --leave --dubleave test4 2>test4.err &&
	grep "not a member" test4.err
'

test_expect_success 'join on all ranks works' '
	flux exec ${GROUPSCMD} join test5
'

test_expect_success 'barrier test using groups works' '
	flux exec ${GROUPSCMD} barrier barrier.1
'
test_expect_success 'ensure barrier count reaches zero' '
	run_timeout 10 ${GROUPSCMD} waitfor --count 0 barrier.1
'
test_expect_success 'dump groups logs on rank 0' '
	flux dmesg|grep groups
'

# The overlay.monitor RPC was added so that groups could be a module
# instead of getting callbacks from overlay.c.
badmonitor() {
	flux python -c "import flux; print(flux.Flux().rpc(\"overlay.monitor\").get())"
}
test_expect_success 'a non-streaming overlay.monitor request fails' '
	test_must_fail badmonitor 2>badmonitor.err &&
	grep "Protocol error" badmonitor.err
'

test_done
