#!/bin/sh
#

test_description='Test various flux utilities

Verify basic functionality of generic flux utils
'

. `dirname $0`/sharness.sh
SIZE=4
LASTRANK=$((SIZE-1))
test_under_flux ${SIZE} kvs

test_expect_success 'event: can publish' '
	run_timeout 5 \
	  $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
	    testcase testcase.foo \
	    flux event pub testcase.foo > output_event_pub &&
	cat >expected_event_pub <<-EOF &&
	testcase.foo
	EOF
	test_cmp expected_event_pub output_event_pub
'
test_expect_success 'event: can subscribe' '
	flux event sub --count=1 hb >output_event_sub &&
	grep "^hb" output_event_sub
'

test_expect_success 'version: reports an expected string' '
        set -x
	flux version | grep -q "flux-core-[0-9]+\.[0-9]+\.[0-9]"
	set +x
'

# heaptrace is only enabled if configured --with-tcmalloc
#   so ENOSYS is a valid response.  We are at least testing that code path.
test_expect_success 'heaptrace start' '
	output=$(flux heaptrace start heaptrace.out 2>&1) \
		|| echo $output | grep -q "Function not implemented"
	output=$(flux heaptrace dump "No reason" 2>&1) \
		|| echo $output | grep -q "Function not implemented"
	output=$(flux heaptrace stop 2>&1) \
		|| echo $output | grep -q "Function not implemented"
'


test_done
