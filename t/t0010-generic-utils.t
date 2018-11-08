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

test_expect_success 'version: reports expected values under an instance' '
	flux version >version.out &&
	grep -q libflux-core version.out &&
	grep -q commands     version.out &&
	grep -q broker       version.out &&
	grep -q FLUX_URI     version.out
'
test_expect_success 'version: reports expected values not under an instance' '
	(unset FLUX_URI; flux version >version2.out) &&
	grep -q libflux-core version2.out &&
	grep -q commands     version2.out &&
	! grep -q broker     version2.out &&
	! grep -q FLUX_URI   version2.out
'

heaptrace_error_check()
{
	output=$(flux heaptrace "$@" 2>&1) \
		|| echo $output | grep -q "Function not implemented"
}

# heaptrace is only enabled if configured --with-tcmalloc
#   so ENOSYS is a valid response.  We are at least testing that code path.
test_expect_success 'heaptrace start' '
	heaptrace_error_check start heaptrace.out &&
	heaptrace_error_check dump "No reason" &&
	heaptrace_error_check stop
'


test_done
