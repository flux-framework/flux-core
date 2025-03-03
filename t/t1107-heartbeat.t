#!/bin/sh
#

test_description='Test heartbeat module'

. `dirname $0`/sharness.sh
SIZE=1
test_under_flux ${SIZE} minimal


test_expect_success 'load heartbeat' '
	flux module load heartbeat
'

test_expect_success 'reload heartbeat with period=10s and verify' '
	period1=$(flux module stats heartbeat | jq -r -e .period) &&
	flux module reload heartbeat period=10s &&
	period2=$(flux module stats heartbeat | jq -r -e .period) &&
	echo period changed from $period1 to $period2 &&
	test "$period1" != "$period2"
'

test_expect_success 'unload heartbeat' '
	flux module unload heartbeat
'

test_expect_success 'reload heartbeat with period=bad fsd' '
	test_must_fail flux module load heartbeat period=1x
'

test_expect_success 'reload heartbeat with bad option' '
	test_must_fail flux module load heartbeat foo=42
'

test_done
