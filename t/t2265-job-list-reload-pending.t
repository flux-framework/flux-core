#!/bin/sh

test_description='Test job-list pending job order across module reload'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

# List pending job IDs in job-list order.
list_pending() {
	flux job list -s pending | jq .id
}

# Convert job IDs to the decimal form returned by flux job list.
decimal_ids() {
	for id in "$@"; do
		flux job id $id || return 1
	done
}

# Wait for job-list to report pending jobs in the order given in file $1,
# since job-list is eventually consistent with the job-manager.
wait_pending_order() {
	test_wait_until "list_pending >pending.out && cmp -s $1 pending.out"
}

test_expect_success 'stop the queue so that jobs remain pending' '
	flux queue stop
'
test_expect_success 'submit jobs with mixed urgencies' '
	a1=$(flux submit --urgency=16 true) &&
	a2=$(flux submit --urgency=0 true) &&
	a3=$(flux submit --urgency=31 true) &&
	a4=$(flux submit --urgency=20 true) &&
	a5=$(flux submit --urgency=16 true) &&
	a6=$(flux submit --urgency=20 true) &&
	a7=$(flux submit --urgency=0 true) &&
	a8=$(flux submit --urgency=31 true) &&
	flux job list-ids --wait-state=sched \
		$a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 >/dev/null
'
# Changing urgency adds priority events to the job history, so the
# backlog replays more than one priority for these jobs.
test_expect_success 'change urgency of two pending jobs' '
	flux job urgency $a1 25 &&
	flux job urgency $a4 10 &&
	decimal_ids $a3 $a8 $a1 $a6 $a5 $a4 $a2 $a7 >expected1 &&
	wait_pending_order expected1
'
test_expect_success 'reload job-list' '
	flux module reload job-list
'
test_expect_success 'pending jobs are in the same order after reload' '
	list_pending >pending.out &&
	test_cmp expected1 pending.out
'
test_expect_success 'urgency change after reload reorders the list' '
	flux job urgency $a7 31 &&
	decimal_ids $a3 $a7 $a8 $a1 $a6 $a5 $a4 $a2 >expected2 &&
	wait_pending_order expected2 &&
	test_cmp expected2 pending.out
'
test_expect_success 'canceled job is removed from the pending list' '
	flux cancel $a5 &&
	flux job list-ids --wait-state=inactive $a5 >/dev/null &&
	decimal_ids $a3 $a7 $a8 $a1 $a6 $a4 $a2 >expected3 &&
	list_pending >pending.out &&
	test_cmp expected3 pending.out
'
test_done
