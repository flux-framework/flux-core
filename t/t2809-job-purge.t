#!/bin/sh

test_description='Test flux job purge'

. $(dirname $0)/sharness.sh

test_under_flux 1 full

# Get the number of inactive jobs
inactive_count() {
	local how=$1

	if test $how = "job-manager"; then
		flux module stats --parse=inactive_jobs job-manager
	elif test $how = "job-list"; then
		flux jobs --no-header --filter=inactive|wc -l
	elif test $how = "job-list-stats"; then
		flux job stats | jq .job_states.inactive
	else
		echo Unknown method: $how >&2
		return 1
	fi
}

# Poll for a specific number of inactive jobs
# Usage: wait_inactive_count method target tries
# where method is job-manager, job-list, or job-list-stats (jq required)
wait_inactive_count() {
	local how=$1
	local target=$2
	local tries=$3
	local count
	while test $tries -gt 0; do
		count=$(inactive_count $how)
		echo $count inactive jobs >&2
		test $count -eq $target && return 0
		sleep 0.25
		tries=$(($tries-1))
	done
	return 1
}

# Speed up heartbeat-driven purge results to make the test run faster
test_expect_success 'reload heartbeat module with fast rate' '
        flux module reload heartbeat period=0.1s
'
test_expect_success 'create 10 inactive jobs' '
	flux submit --cc=1-10 /bin/true >jobids &&
	flux queue drain
'
test_expect_success 'verify job KVS eventlogs exist' '
	for id in $(cat jobids); do \
	    flux job eventlog $id >/dev/null;  \
	done
'
test_expect_success 'flux job purge with no args says use force' '
	flux job purge >noargs.out &&
	grep "use --force to purge" noargs.out
'
test_expect_success 'flux job purge with no limits purges 0' '
	flux job purge --force >noforce.out &&
	grep "purged 0 inactive jobs" noforce.out
'
test_expect_success 'flux job purge --batch=10000 fails' '
	test_must_fail flux job purge --force --batch=10000 2>bigbatch.err &&
	grep "batch must be" bigbatch.err
'
test_expect_success 'flux job purge --force --num-limit=-42 fails' '
	test_must_fail flux job purge --num-limit=-42 2>negnum.err &&
	grep "num limit must be" negnum.err
'
test_expect_success 'flux job purge with extra free argument fails' '
	test_must_fail flux job purge xyz 2>freearg.err &&
	grep "Usage:" freearg.err
'
test_expect_success 'flux job purge --num-limit=8 would purge 2' '
	flux job purge --num-limit=8 >num8.out &&
	grep "use --force to purge 2 " num8.out
'
test_expect_success 'flux job purge --force --num-limit=8 purges 2' '
	flux job purge --force --num-limit=8 >num8f.out &&
	grep "purged 2 inactive jobs" num8f.out
'
test_expect_success 'flux job purge --num-limit=8 would purge 0' '
	flux job purge --num-limit=8 >num8_again.out &&
	grep "use --force to purge 0 " num8_again.out
'
test_expect_success 'flux job purge --force --num-limit=8 purges 0' '
	flux job purge --force --num-limit=8 >num8f_again.out &&
	grep "purged 0 inactive jobs" num8f_again.out
'
test_expect_success 'flux job purge --num-limit=6 would purge 2' '
	flux job purge --num-limit=6 --batch=1 >num6.out &&
	grep "use --force to purge 2 " num6.out
'
test_expect_success 'flux job purge --force --num-limit=6 purges 2' '
	flux job purge --force --num-limit=6 --batch=1 >num6f.out &&
	grep "purged 2 inactive jobs" num6f.out
'
test_expect_success 'flux job purge --force --num-limit=1000 --age-limit=1ms purges 6' '
	flux job purge --force --age-limit=1ms >both.out &&
	grep "purged 6 inactive jobs" both.out
'
test_expect_success 'flux job purge --force --num-limit=1 purges 0' '
	flux job purge --force --num-limit=1 >num1.out &&
	grep "purged 0 inactive jobs" num1.out
'
test_expect_success 'verify job KVS eventlogs do not exist' '
	for id in $(cat jobids); do \
	    test_must_fail flux job eventlog $id; \
	done
'
test_expect_success 'create 2 inactive jobs with known completion order' '
	flux submit /bin/true >jobid1 &&
	flux job wait-event $(cat jobid1) clean &&
	flux submit /bin/true >jobid2 &&
	flux job wait-event $(cat jobid2) clean
'
test_expect_success 'purge the oldest job - youngest is still there' '
	flux job purge --force --num-limit=1 &&
	flux job eventlog $(cat jobid2) >/dev/null
'
test_expect_success 'purge the last job' '
	flux job purge --force --num-limit=0 &&
	wait_inactive_count job-manager 0 30
'
test_expect_success 'create 10 inactive jobs' '
	flux submit --cc=1-10 /bin/true &&
	flux queue drain
'
test_expect_success 'reconfigure job manager with inactive-num-limit=5' '
	flux config load <<-EOT
	[job-manager]
	inactive-num-limit = 5
	EOT
'
test_expect_success 'wait for job-list inactive job count to reach 5' '
	wait_inactive_count job-list 5 30
'
test_expect_success NO_CHAIN_LINT 'run multiple flux-job purges concurrently' '
	flux job purge --force --num-limit=4 &
	pid=$! &&
	flux job purge --force --num-limit=3 &&
	wait $pid
'
test_expect_success NO_CHAIN_LINT 'wait for job-list inactive job count to reach 3' '
	wait_inactive_count job-list 3 30
'
test_expect_success 'reconfigure job manager with inactive-age-limit=1ms' '
	flux config load <<-EOT
	[job-manager]
	inactive-age-limit = "1ms"
	EOT
'
test_expect_success 'wait for job-list inactive job count to reach 0' '
	wait_inactive_count job-list 0 30
'
test_expect_success HAVE_JQ 'confirm job-list stats show zero inactive jobs' '
	test $(inactive_count job-list-stats) -eq 0
'
test_expect_success 'reconfigure job manager with incorrect type limit' '
	test_must_fail flux config load 2>badtype.err <<-EOT &&
	[job-manager]
	inactive-age-limit = 42
	EOT
	grep "Expected string" badtype.err
'
test_expect_success 'reconfigure job manager with bad age-limit fsd' '
	test_must_fail flux config load 2>badfsd.err <<-EOT &&
	[job-manager]
	inactive-age-limit = "notfsd"
	EOT
	grep "invalid FSD" badfsd.err
'
test_expect_success 'reconfigure job manager with invalid num-limit' '
	test_must_fail flux config load 2>badnum.err <<-EOT &&
	[job-manager]
	inactive-num-limit = -42
	EOT
	grep "must be >= 0" badnum.err
'
test_expect_success 'new instance with bad config fails to start' '
	mkdir -p config &&
	cat >config/system.toml <<-EOT &&
	[job-manager]
	inactive-num-limit = -42
	EOT
	test_must_fail flux start -o,--config-path=$(pwd)/config \
		/bin/true 2>badnum2.err &&
	grep "must be >= 0" badnum2.err
'

test_done
