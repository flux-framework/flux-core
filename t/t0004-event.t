#!/bin/sh
#

test_description='Test event propagation'

. `dirname $0`/sharness.sh
SIZE=4
LASTRANK=$(($SIZE-1))
test_under_flux ${SIZE} minimal

test_expect_success 'heartbeat is received on all ranks' '
	run_timeout 5 \
          flux exec -n flux event sub --count=1 hb >output_event_sub &&
	hb_count=`grep "^hb" output_event_sub | wc -l` &&
        test $hb_count -eq $SIZE
'

test_expect_success 'events from rank 0 received correctly on rank 0' '
	run_timeout 5 \
          $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
            testcase testcase.eof \
	      $SHARNESS_TEST_SRCDIR/scripts/t0004-event-helper.sh 0 >trace &&
        $SHARNESS_TEST_SRCDIR/scripts/t0004-event-helper.sh >trace.expected &&
        test_cmp trace.expected trace
'

test_expect_success "events from rank $LASTRANK received correctly on rank 0" '
	run_timeout 5 \
          $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
            testcase testcase.eof \
	      $SHARNESS_TEST_SRCDIR/scripts/t0004-event-helper.sh $LASTRANK >trace &&
        $SHARNESS_TEST_SRCDIR/scripts/t0004-event-helper.sh >trace.expected &&
        test_cmp trace.expected trace
'

test_expect_success 'publish event with no payload (loopback)' '
	run_timeout 5 flux event pub -l foo.bar
'

test_expect_success 'publish event with JSON payload (loopback)' '
	run_timeout 5 flux event pub -l foo.bar {}
'

test_expect_success 'publish event with raw payload (loopback)' '
	run_timeout 5 flux event pub -l -r foo.bar foo
'

test_expect_success 'publish private event with JSON payload (loopback)' '
	run_timeout 5 flux event pub -l foo.bar {}
'

test_expect_success 'publish private event with raw payload (loopback)' '
	run_timeout 5 flux event pub -p -l -r foo.bar foo
'

test_expect_success 'publish event with no payload (synchronous)' '
	run_timeout 5 flux event pub -s foo.bar
'

test_expect_success 'publish event with JSON payload (synchronous)' '
	run_timeout 5 flux event pub -s foo.bar {}
'

test_expect_success 'publish event with raw payload (synchronous)' '
	run_timeout 5 flux event pub -s -r foo.bar foo
'

test_expect_success 'publish private event with JSON payload (synchronous)' '
	run_timeout 5 flux event pub -p -s foo.bar {}
'

test_expect_success 'publish private event with raw payload (synchronous)' '
	run_timeout 5 flux event pub -p -s -r foo.bar foo
'

test_expect_success 'publish private event with no payload (synchronous,loopback)' '
	run_timeout 5 flux event pub -p -s -l foo.bar
'

test_done
