#!/bin/sh
#

test_description='Test the instance name chosen by each bootstrap method

An instance name must be unique among the Flux instances running as one user
on a node, since it identifies resources they create outside the broker.  No
launcher assigns it here, so each bootstrap method picks one, and this test
covers those choices, their uniqueness, which names may be set by hand, and
how a subinstance extends the name of the instance that launched it.
'

. `dirname $0`/sharness.sh

# Avoid loading unnecessary modules in back to back broker tests
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

#
# singleton bootstrap
#
test_expect_success 'singleton instance is named for the broker pid' '
	flux start ${ARGS} flux getattr instance-name >single.out &&
	test_debug "cat single.out" &&
	grep -E "^single-[0-9]+$" single.out
'

test_expect_success 'two singleton instances get different names' '
	flux start ${ARGS} flux getattr instance-name >single2.out &&
	test_must_fail test_cmp single.out single2.out
'

#
# flux-start test bootstrap
#
test_expect_success 'test instance is named for the flux-start pid' '
	flux start -s1 ${ARGS} flux getattr instance-name >test.out &&
	test_debug "cat test.out" &&
	grep -E "^test-[0-9]+$" test.out
'

test_expect_success 'two test instances get different names' '
	flux start -s1 ${ARGS} flux getattr instance-name >test2.out &&
	test_must_fail test_cmp test.out test2.out
'

test_expect_success 'every broker in a test instance agrees on the name' '
	flux start -s4 ${ARGS} \
	    flux exec -r all flux getattr instance-name >test4.out &&
	test_debug "cat test4.out" &&
	test $(sort -u test4.out | wc -l) -eq 1
'

test_expect_success '--test-instance-name sets the name' '
	echo fubar >named.exp &&
	flux start -s1 ${ARGS} --test-instance-name=fubar \
	    flux getattr instance-name >named.out &&
	test_cmp named.exp named.out
'

test_expect_success '--test-instance-name requires --test-size' '
	test_must_fail flux start --test-instance-name=fubar true
'

#
# setting the name by hand
#
# -Sinstance-name sets the broker attribute directly, where
# --test-instance-name above reaches it by way of the PMI KVS name.
#
test_expect_success 'instance-name may be set on the broker command line' '
	echo sys >setattr.exp &&
	flux start ${ARGS} -Sinstance-name=sys \
	    flux getattr instance-name >setattr.out &&
	test_cmp setattr.exp setattr.out
'

test_expect_success 'instance-name is immutable' '
	test_must_fail flux start ${ARGS} \
	    flux setattr instance-name foo
'

# A name given by hand is rejected rather than adjusted, so that whoever
# sets one can rely on finding that same name later.
test_expect_success 'broker rejects an instance-name it cannot use' '
	test_must_fail flux start ${ARGS} -Sinstance-name=foo/bar true 2>bad.err &&
	grep "instance-name must be" bad.err
'

test_expect_success 'broker rejects an empty instance-name' '
	test_must_fail flux start ${ARGS} -Sinstance-name= true
'

test_expect_success 'broker rejects an over-long instance-name' '
	long=$(printf "a%.0s" $(seq 64)) &&
	test_must_fail flux start ${ARGS} -Sinstance-name=$long true
'

#
# config file bootstrap
#
# The system instance is conventionally the only config bootstrapped instance
# running as its user on a node, so it takes a fixed name.  A recovery
# instance reads the same config and so identifies itself the same way.
#
test_expect_success 'config bootstrap instance is named sys' '
	mkdir conf1 &&
	cat <<-EOT >conf1/bootstrap.toml &&
	[bootstrap]
	EOT
	echo sys >conf1.exp &&
	flux broker ${ARGS} -c conf1 flux getattr instance-name >conf1.out &&
	test_cmp conf1.exp conf1.out
'

#
# nested instances
#
# A subinstance appends its job ID to the enclosing instance name, so that
# names unique at the top level stay unique at every depth.  The enclosing
# instances here run jobs, so only the innermost is started with ARGS.
#
test_expect_success 'a subinstance appends its job id to the parent name' '
	flux start -s1 --test-instance-name=parent \
	    flux run -n1 flux start ${ARGS} \
	        flux getattr instance-name >nest1.out &&
	test_debug "cat nest1.out" &&
	grep -E "^parent-f[1-9A-HJ-NP-Za-km-z]+$" nest1.out
'

test_expect_success 'a second level appends again' '
	flux start -s1 --test-instance-name=parent \
	    flux run -n1 flux start \
	        flux run -n1 flux start ${ARGS} \
	            flux getattr instance-name >nest2.out &&
	test_debug "cat nest2.out" &&
	grep -E "^parent-f[1-9A-HJ-NP-Za-km-z]+-f[1-9A-HJ-NP-Za-km-z]+$" nest2.out
'

test_done
