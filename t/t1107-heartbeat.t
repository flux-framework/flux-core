#!/bin/sh
#

test_description='Test heartbeat module'

. `dirname $0`/sharness.sh
SIZE=1
test_under_flux ${SIZE} minimal


test_expect_success 'load heartbeat, period is 2s' '
	flux module load heartbeat &&
	flux module stats heartbeat | jq -r -e ".period == 2"
'
test_expect_success 'reload heartbeat with period=10s' '
	flux module reload heartbeat period=10s &&
	flux module stats heartbeat | jq -r -e ".period == 10"
'
test_expect_success 'reconfig with period=5s' '
	flux config load <<-EOT &&
	[heartbeat]
	period = "5s"
	EOT
	flux module stats heartbeat | jq -r -e ".period == 5"
'
test_expect_success 'reconfig with wrong period type, period is still 5s' '
	test_must_fail flux config load <<-EOT &&
	[heartbeat]
	period = 4
	EOT
	flux module stats heartbeat | jq -r -e ".period == 5"
'
test_expect_success 'reconfig with bad period FSD, period is still 5s' '
	test_must_fail flux config load <<-EOT &&
	[heartbeat]
	period = "zzz"
	EOT
	flux module stats heartbeat | jq -r -e ".period == 5"
'
test_expect_success 'reconfig with bad key' '
	test_must_fail flux config load <<-EOT
	[heartbeat]
	z = 42
	EOT
'
test_expect_success 'reconfig with empty table, period is 2s' '
	flux config load <<-EOT &&
	[heartbeat]
	EOT
	flux module stats heartbeat | jq -r -e ".period == 2"
'
test_expect_success 'unload heartbeat' '
	flux module unload heartbeat
'
test_expect_success 'reconfig period of 0 (unchecked)' '
	flux config load <<-EOT
	[heartbeat]
	period = "0"
	EOT
'
test_expect_success 'loading heartbeat fails with bad config' '
	test_must_fail flux module load heartbeat
'
test_expect_success 'reconfig with empty [heartbeat] table' '
	flux config load <<-EOT
	[heartbeat]
	EOT
'
test_expect_success 'load heartbeat, period is 2s' '
	flux module load heartbeat &&
	flux module stats heartbeat | jq -r -e ".period == 2"
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
test_expect_success 'load heartbeat with period=1s' '
	flux module load heartbeat period=1s
'
test_expect_success 'period is 1s' '
	flux module stats heartbeat | jq -r -e ".period == 1"
'
test_expect_success 'unload heartbeat' '
	flux module unload heartbeat
'

test_done
