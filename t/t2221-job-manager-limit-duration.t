#!/bin/sh

test_description='Test flux job manager limit-duration plugin'

. $(dirname $0)/sharness.sh

test_under_flux 2 full

flux setattr log-stderr-level 1

test_expect_success 'configuring an invalid duration limit fails' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	duration = 1.0
	EOT
'
test_expect_success 'configure a valid duration limit' '
	flux config load <<-EOT
	[policy.limits]
	duration = "1m"
	EOT
'
test_expect_success 'a job that exceeds policy.limits.duration is rejected' '
	test_must_fail flux submit -t 1h true 2>duration.err &&
	grep "exceeds policy limit of 1m" duration.err
'
test_expect_success 'a job that is under policy.limits.duration is accepted' '
	flux submit -t 30s true
'
test_expect_success 'configure policy.limits.duration and queue duration' '
	flux config load <<-EOT &&
	[policy.limits]
	duration = "1h"
	[queues.debug]
	[queues.batch.policy.limits]
	duration = "8h"
	[queues.short.policy.limits]
	duration = "1m"
	EOT
	flux queue start --all
'
test_expect_success 'a job that exceeds policy.limits.duration is rejected' '
	test_must_fail flux submit --queue=debug -t 2h true 2>limit.err &&
	test_debug "cat limit.err"
'
test_expect_success 'error message includes expected details' '
	grep "duration (2h) exceeds.*limit of 1h for queue debug" limit.err
'
test_expect_success 'a job with no limit is also rejected' '
	test_must_fail flux submit --queue=debug -t 0 true 2>limit2.err &&
	test_debug "cat limit2.err"
'
test_expect_success 'error message includes expected details' '
	grep "duration (unlimited) exceeds.*limit of 1h for queue debug" limit2.err
'
test_expect_success 'but is accepted by a queue with higher limit' '
	flux submit \
	    --queue=batch \
	    -t 2h \
	    true
'
test_expect_success 'and is rejected when it exceeds the queue limit' '
	test_must_fail flux submit \
	    --queue=batch \
	    -t 16h \
	    true
'
test_expect_success 'no limit is also rejected as exceeding the queue limit' '
	test_must_fail flux submit \
	    --queue=batch \
	    -t 0 \
	    true
'
test_expect_success 'a job that is under policy.limits.duration is accepted' '
	flux submit --queue=debug -t 1h true
'
test_expect_success 'but is rejected on a queue with lower limit' '
	test_must_fail flux submit \
	    --queue=short \
	    -t 1h \
	    true
'
test_expect_success 'configure policy.limits.duration and an unlimited queue' '
	flux config load <<-EOT
	[policy.limits]
	duration = "1h"
	[queues.batch.policy.limits]
	duration = "0"
	[queues.debug]
	EOT
'
test_expect_success 'a job that is over policy.limits.duration is rejected' '
	test_must_fail flux submit --queue=debug -t 2h true
'
test_expect_success 'but is accepted by the unlimited queue' '
	flux submit \
	    --queue=batch \
	    -t 2h true
'
test_expect_success 'a job that sets no explicit duration is accepted by the unlimited queue' '
	flux submit \
	    --queue=batch \
	    true
'
test_expect_success 'configure an invalid duration limit' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	duration = "xyz123"
	EOT
'
test_expect_success 'configure a duration limit of an invalid type' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	duration = [ 0 ]
	EOT
'
test_expect_success 'configure an invalid queue duration limit' '
	test_must_fail flux config load <<-EOT
	[queues.debug.policy.limits]
	duration = "xyz123"
	EOT
'

test_done
