#!/bin/sh
#

test_description='Test the jobid-path root chosen by each bootstrap method

jobid-path begins with the name of the top level instance, which must be
unique among the Flux instances running as one user on a node, since the
path identifies resources they create outside the broker.  No launcher
assigns a name here, so each bootstrap method picks one, and this test
covers those choices, their uniqueness, and how subinstances extend the
path.
'

. `dirname $0`/sharness.sh

# Avoid loading unnecessary modules in back to back broker tests
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

#
# singleton bootstrap
#
test_expect_success 'singleton is named for the broker pid' '
	flux start ${ARGS} flux getattr jobid-path >single.out &&
	test_debug "cat single.out" &&
	grep -E "^/single-[0-9]+$" single.out
'

test_expect_success 'two singleton instances get different names' '
	flux start ${ARGS} flux getattr jobid-path >single2.out &&
	test_must_fail test_cmp single.out single2.out
'

#
# flux-start test bootstrap
#
test_expect_success 'test instance is named for the flux-start pid' '
	flux start -s1 ${ARGS} flux getattr jobid-path >test.out &&
	test_debug "cat test.out" &&
	grep -E "^/test-[0-9]+$" test.out
'

test_expect_success 'two test instances get different names' '
	flux start -s1 ${ARGS} flux getattr jobid-path >test2.out &&
	test_must_fail test_cmp test.out test2.out
'

test_expect_success 'every broker in a test instance agrees on the path' '
	flux start -s4 ${ARGS} \
	    flux exec -r all flux getattr jobid-path >test4.out &&
	test_debug "cat test4.out" &&
	test $(sort -u test4.out | wc -l) -eq 1
'

test_expect_success '--test-instance-name sets the name' '
	echo /fubar >named.exp &&
	flux start -s1 ${ARGS} --test-instance-name=fubar \
	    flux getattr jobid-path >named.out &&
	test_cmp named.exp named.out
'

test_expect_success '--test-instance-name requires --test-size' '
	test_must_fail flux start --test-instance-name=fubar true
'

# "/" is the jobid-path separator and whitespace cannot cross the PMI wire,
# so names that would corrupt the path are rejected up front.
test_expect_success '--test-instance-name rejects a slash' '
	test_must_fail flux start -s1 ${ARGS} --test-instance-name=a/b true
'

test_expect_success '--test-instance-name rejects whitespace' '
	test_must_fail flux start -s1 ${ARGS} --test-instance-name="a b" true
'

test_expect_success '--test-instance-name rejects an empty name' '
	test_must_fail flux start -s1 ${ARGS} --test-instance-name= true
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
	echo /sys >conf1.exp &&
	flux broker ${ARGS} -c conf1 flux getattr jobid-path >conf1.out &&
	test_cmp conf1.exp conf1.out
'

#
# nested instances
#
# A subinstance appends its job ID to the enclosing instance path, so paths
# unique at the top level stay unique at every depth.  The enclosing
# instances here run jobs, so only the innermost is started with ARGS.
#
test_expect_success 'a subinstance appends its job id to the parent path' '
	flux start -s1 --test-instance-name=parent \
	    flux run -n1 flux start ${ARGS} \
	        flux getattr jobid-path >nest1.out &&
	test_debug "cat nest1.out" &&
	grep -E "^/parent/(ƒ|f)[^/]+$" nest1.out
'

test_expect_success 'a second level appends again' '
	flux start -s1 --test-instance-name=parent \
	    flux run -n1 flux start \
	        flux run -n1 flux start ${ARGS} \
	            flux getattr jobid-path >nest2.out &&
	test_debug "cat nest2.out" &&
	grep -E "^/parent/(ƒ|f)[^/]+/(ƒ|f)[^/]+$" nest2.out
'

test_done
