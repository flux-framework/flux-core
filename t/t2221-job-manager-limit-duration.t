#!/bin/sh

test_description='Test flux job manager limit-duration plugin'

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 2 full -o,--config-path=$(pwd)/config

flux setattr log-stderr-level 1

test_expect_success 'configuring an invalid duration limit fails' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = 1.0
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure a valid duration limit' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = "1m"
	EOT
	flux config reload
'
test_expect_success 'a job that exceeds policy.limits.duration is rejected' '
	test_must_fail flux mini submit -t 1h /bin/true 2>duration.err &&
	grep "exceeds policy limit of 1m" duration.err
'
test_expect_success 'a job that sets no explicit duration is accepted' '
	flux mini submit /bin/true
'
test_expect_success 'a job that is under policy.limits.duration is accepted' '
	flux mini submit -t 30s /bin/true
'
test_expect_success 'configure policy.limits.duration and queue durations' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = "1h"
	[queues.pdebug.policy.limits]
	duration = "1m"
	[queues.pbatch.policy.limits]
	duration = "8h"
	EOT
	flux config reload
'
test_expect_success 'a no-queue job that exceeds policy.limits.duration is rejected' '
	test_must_fail flux mini submit -t 2h /bin/true
'
test_expect_success 'but is accepted by a queue with higher limit' '
	flux mini submit \
	    --queue=pbatch \
	    -t 2h \
	    /bin/true
'
test_expect_success 'and is rejected when it exceeds the queue limit' '
	test_must_fail flux mini submit \
	    --queue=pbatch \
	    -t 16h \
	    /bin/true
'
test_expect_success 'a job that is under policy.limits.duration is accepted' '
	flux mini submit -t 1h /bin/true
'
test_expect_success 'but is rejected on a queue with lower limit' '
	test_must_fail flux mini submit \
	    --queue=pdebug \
	    -t 1h \
	    /bin/true
'
test_expect_success 'configure policy.limits.duration and an unlimited queue' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = "1h"
	[queues.pdebug.policy.limits]
	duration = "0"
	EOT
	flux config reload
'
test_expect_success 'a job that is over policy.limits.duration is rejected' '
	test_must_fail flux mini submit -t 2h /bin/true
'
test_expect_success 'but is accepted by the unlimited queue' '
	flux mini submit \
	    --queue=pdebug \
	    -t 2h /bin/true
'
test_expect_success 'a job that sets no explicit duration is accepted by the unlimited queue' '
	flux mini submit \
	    --queue=pdebug \
	    /bin/true
'
test_expect_success 'configure an invalid duration limit' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = "xyz123"
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure a duration limit of an invalid type' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	duration = [ 0 ]
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure an invalid queue duration limit' '
	cat >config/policy.toml <<-EOT &&
	[queues.pdebug.policy.limits]
	duration = "xyz123"
	EOT
	test_must_fail flux config reload
'

test_done
