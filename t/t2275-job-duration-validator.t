#!/bin/sh

test_description='Test job duration validator plugin in job-manager'

. $(dirname $0)/sharness.sh

test_under_flux 1 job -Slog-stderr-level=1

test_expect_success 'duration: no validation when there is no expiration' '
	flux submit -t 100d true
'

wait_duration_update() {
	local value=$1
	local n=10
	while test $n -gt 0; do
		flux dmesg | grep "duration-validator:.*$value" && return 0
	done
	return 1
}
test_expect_success 'duration: set an expiration on resource.R' '
	expiration=$(($(date +%s)+60)) &&
	flux kvs put \
	    resource.R="$(flux kvs get resource.R | \
			jq -S .execution.expiration=$expiration)" &&
	wait_duration_update $expiration
'
test_expect_success 'duration: submit a job without duration' '
	flux submit true
'
test_expect_success 'duration: submit a job with duration below expiration' '
	flux submit -t 5s true
'
test_expect_success 'duration: submit a job with duration after expiration' '
	test_must_fail flux submit -t 1h true
'
test_done
