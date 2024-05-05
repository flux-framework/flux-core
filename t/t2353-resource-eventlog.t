#!/bin/sh

test_description='Test resource eventlog upgrade

Use the actual eventlog from test systems to see how the
upgrade to v0.62.0 is going to go.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_QUORUM=1
export TEST_UNDER_FLUX_START_MODE=leader # only start rank 0

SIZE=16384  # large instance to accommodate all test input
test_under_flux $SIZE system

test_expect_success 'test size is correct' '
	test $(flux getattr size) -eq $SIZE
'
test_expect_success 'everything is down but rank 0' '
	flux uptime | grep "$(($SIZE-1)) offline"
'
test_expect_success 'unload scheduler' '
	flux module remove sched-simple
'

for eventlog in ${SHARNESS_TEST_SRCDIR}/resource/resource.eventlog.*; do
	filename=$(basename $eventlog)
	test_expect_success "load test resources $filename" '
		flux dmesg -C &&
		flux kvs put --raw resource.eventlog=- <$eventlog &&
		flux module reload resource noverify
	'
	test_expect_success 'eventlog upgrade reduced entry count' '
		flux dmesg | grep "resource.eventlog: reduced"
	'
	test_expect_success 'reloading resource module does not rewrite log' '
		flux dmesg -C &&
		flux module reload resource noverify &&
		flux dmesg >dmesg.out &&
		test_must_fail grep "resource.eventlog: reduced" dmesg.out
	'
	test_expect_success 'parse eventlog entry names' '
		flux kvs get --raw resource.eventlog >$filename &&
		jq -r -e ".name" <$filename >$filename.names
	'
	test_expect_success 'eventlog now contains no legacy events' '
		test_must_fail grep -E "offline|online|resource-init" \
		    $filename.names
	'
	# one orig resource-define plus two resource loads in the test
	test_expect_success 'eventlog contains 3 resource-define events' '
		count=$(grep "resource-define" $filename.names | wc -l) &&
		test $count -eq 3
	'
done
test_expect_success 'load scheduler' '
	flux module load sched-simple
'

test_done
