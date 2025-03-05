#!/bin/sh
#

test_description='Test heartbeat module'

. `dirname $0`/sharness.sh
SIZE=2
test_under_flux ${SIZE} minimal

test_expect_success 'load heartbeat, period is 2s' '
	flux exec flux module load heartbeat &&
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

# Cover heartbeat.timeout

test_expect_success 'reconfig with timeout=infinity works' '
	flux config load <<-EOT &&
	[heartbeat]
	timeout = "infinity"
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == -1"
'
test_expect_success 'reconfig with timeout=0 works' '
	flux config load <<-EOT &&
	[heartbeat]
	timeout = "0"
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == -1"
'
test_expect_success 'reconfig with timeout=1m works' '
	flux config load <<-EOT &&
	[heartbeat]
	period = "2s"
	timeout = "1m"
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == 60"
'
test_expect_success 'reconfig with wrong timeout type fails' '
	test_must_fail flux config load <<-EOT &&
	[heartbeat]
	period = "2s"
	timeout = 42
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == 60"
'
test_expect_success 'reconfig with bad timeout FSD fails' '
	test_must_fail flux config load <<-EOT &&
	[heartbeat]
	period = "2s"
	timeout = "42z"
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == 60"
'
test_expect_success 'reconfig with timeout < period fails' '
	test_must_fail flux config load <<-EOT &&
	[heartbeat]
	period = "2s"
	timeout = "1s"
	EOT
	flux module stats heartbeat | jq -r -e ".timeout == 60"
'
test_expect_success 'reconfig with warn_thresh=-1 fails' '
	test_must_fail flux config load <<-EOT
	[heartbeat]
	warn_thresh = -1
	EOT
'
test_expect_success 'reconfig with warn_thresh=10 works' '
	flux config load <<-EOT &&
	[heartbeat]
	warn_thresh = 10
	EOT
	flux module stats heartbeat | jq -r -e ".warn_thresh == 10"
'
test_expect_success 'reload with period=0.1s timeout=infinity warn_thresh=3' '
	flux exec flux setattr log-stderr-level 4 &&
	flux exec -r 1 flux dmesg -C &&
	flux exec -r 1 flux config load <<-EOT &&
	[heartbeat]
	period = "0.1s"
	timeout = "infinity"
	warn_thresh = 3
	EOT
	flux exec -r 1 flux module reload heartbeat &&
	flux exec -r 1 flux module stats heartbeat
'
test_expect_success 'stop leader broker and get follower log messages' '
	flux module remove heartbeat &&
	sleep 2 &&
	flux module load heartbeat &&
	flux exec -r 1 flux dmesg -H >dmesg.log
'
test_expect_success 'heartbeat overdue was logged' '
	grep -q "heartbeat overdue" dmesg.log
'
test_expect_success 'unload heartbeat' '
	flux exec flux module unload heartbeat
'
test_done
