#!/bin/sh

test_description='test scheduler generator protocol and stats-get RPC'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

SCHED_SLOWGEN=${SHARNESS_TEST_SRCDIR}/scheduler/sched-slowgen.py

test_under_flux 2 job

# Shorthand: query a single stats field as a number
sched_stat() {
	flux module stats sched-slowgen | jq ".$1"
}

test_expect_success 'unload sched-simple, load sched-slowgen' '
	flux module unload sched-simple &&
	flux module load ${SCHED_SLOWGEN}
'

# -------------------------------------------------------------------
# stats-get: basic shape
# -------------------------------------------------------------------

test_expect_success 'stats-get returns expected fields' '
	flux module stats sched-slowgen >stats.json &&
	jq -e ".sched_passes >= 0" stats.json &&
	jq -e ".sched_yields >= 0" stats.json &&
	jq -e ".forecast_passes >= 0" stats.json &&
	jq -e ".forecast_yields >= 0" stats.json &&
	jq -e ".sched_delay >= 0" stats.json &&
	jq -e ".sched_duration_ewma >= 0" stats.json &&
	jq -e ".sched_interval_ewma >= 0" stats.json &&
	jq -e ".pending_jobs >= 0" stats.json
'

# -------------------------------------------------------------------
# stats-get: sched_passes increments after a scheduling pass
# -------------------------------------------------------------------

test_expect_success 'record sched_passes before submitting a job' '
	sched_stat sched_passes >passes_before.txt
'
test_expect_success 'submit and run a job' '
	flux run -N1 true
'
test_expect_success 'sched_passes incremented after job ran' '
	passes_before=$(cat passes_before.txt) &&
	passes_after=$(sched_stat sched_passes) &&
	test "$passes_after" -gt "$passes_before"
'

# -------------------------------------------------------------------
# stats-get: sched_yields > 0 after a multi-job pass
# Submit more jobs than available slots so the queue is non-trivial.
# The slow scheduler yields after every alloc attempt (including when
# blocked), so sched_yields must be non-zero — confirming the reactor
# received control during the pass.
# -------------------------------------------------------------------

test_expect_success 'unload job-exec to prevent job execution' '
	flux module unload job-exec
'
test_expect_success 'submit 4 jobs on a 2-slot instance' '
	sched_stat sched_passes >passes_before4.txt &&
	for i in $(seq 1 4); do
		flux submit -N1 true
	done
'
test_expect_success 'wait for scheduler to process the queue' '
	passes_before=$(cat passes_before4.txt) &&
	test_wait_until "test \$(sched_stat sched_passes) -gt $passes_before"
'
test_expect_success 'sched_yields > 0 confirms reactor ran during pass' '
	yields=$(sched_stat sched_yields) &&
	test "$yields" -gt 0
'
test_expect_success 'pending_jobs reflects pending jobs' '
	depth=$(sched_stat pending_jobs) &&
	test "$depth" -gt 0
'
test_expect_success 'cancel all pending jobs and reload job-exec' '
	flux cancel --all &&
	flux module load job-exec
'

# -------------------------------------------------------------------
# generator abort/restart: a higher-priority job submitted while a
# pass is in progress is still scheduled correctly.
#
# Fill both slots with long-running jobs, submit two lower-priority
# pending jobs, then submit a higher-priority job.  After the running
# jobs finish, the higher-priority job should run before the others.
# -------------------------------------------------------------------

test_expect_success 'fill both slots with running jobs' '
	flux submit -N1 sleep 3600 >running1.id &&
	flux submit -N1 sleep 3600 >running2.id &&
	flux job wait-event --timeout=10 $(cat running1.id) alloc &&
	flux job wait-event --timeout=10 $(cat running2.id) alloc
'
test_expect_success 'submit two low-priority pending jobs' '
	flux submit -N1 --urgency=1 true >low1.id &&
	flux submit -N1 --urgency=1 true >low2.id
'
test_expect_success 'submit a high-priority job' '
	flux submit -N1 --urgency=31 true >high.id
'
test_expect_success 'cancel the running jobs to free resources' '
	flux cancel $(cat running1.id) &&
	flux cancel $(cat running2.id)
'
test_expect_success 'high-priority job runs before low-priority jobs' '
	run_timeout 30 flux job wait-event $(cat high.id) clean &&
	for id in $(cat low1.id) $(cat low2.id); do
		flux job wait-event --timeout=30 $id clean
	done &&
	high_t=$(flux job eventlog $(cat high.id) | awk "/alloc/{print \$1; exit}") &&
	low1_t=$(flux job eventlog $(cat low1.id) | awk "/alloc/{print \$1; exit}") &&
	awk "BEGIN { exit ($high_t < $low1_t) ? 0 : 1 }"
'

test_expect_success 'reload sched-simple' '
	flux module unload sched-slowgen &&
	flux module load sched-simple
'

test_done
