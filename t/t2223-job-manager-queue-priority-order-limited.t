#!/bin/sh

test_description='Test flux job manager scheduler queue priority ordering (limited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="limited=2"
test_under_flux 1 job -Slog-stderr-level=1


# N.B. resources = 1 rank, 2 cores/rank
# flux queue stop/start to ensure no scheduling until after all jobs submitted
test_expect_success 'job-manager: submit 6 jobs (differing urgencies)' '
        flux queue stop &&
        flux bulksubmit --log=job{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: $(seq 10 2 20) &&
        flux queue start
'

test_expect_success 'job-manager: job state SSSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) R
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_all1.out &&
	cat <<-EOT >counts_all1.exp &&
	Job submission is enabled
	Scheduling is started
	2 alloc requests queued
	2 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_all1.exp counts_all1.out
'

test_expect_success 'job-manager: stop scheduling' '
        flux queue stop
'

test_expect_success 'job-manager: increase urgency job 3' '
        flux job urgency $(cat job3.id) 20
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_all2.out &&
	cat <<-EOT >counts_all2.exp &&
	Job submission is enabled
	Scheduling is stopped
	0 alloc requests queued
	0 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_all2.exp counts_all2.out
'

test_expect_success 'job-manager: cancel a running job' '
        flux cancel $(cat job6.id)
'

test_expect_success 'job-manager: start scheduling' '
        flux queue start
'

# job 3 should run before over previously higher priority job 4
test_expect_success 'job-manager: job state SSRSRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) I
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_all3.out &&
	cat <<-EOT >counts_all3.exp &&
	Job submission is enabled
	Scheduling is started
	1 alloc requests queued
	2 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_all3.exp counts_all3.out
'

test_expect_success 'job-manager: stop scheduling' '
        flux queue stop
'

test_expect_success 'job-manager: increase urgency job 1' '
        flux job urgency $(cat job1.id) 20
'

test_expect_success 'job-manager: cancel a running job' '
        flux cancel $(cat job5.id)
'

test_expect_success 'job-manager: start scheduling' '
        flux queue start
'

# job 1 should have highest remaining priority and now run
test_expect_success 'job-manager: job state RSRSII' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) I &&
        jmgr_check_state $(cat job6.id) I
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux cancel --all --states=pending &&
        flux cancel --all &&
        flux queue drain
'

#
# Test named queues
#

test_expect_success 'configure batch,debug queues' '
	flux config load <<-EOT &&
	[queues.batch]
	[queues.debug]
	EOT
	flux queue enable --all &&
	flux queue start --all
'

test_expect_success 'job-manager: submit 5 jobs to each queue (differing urgencies)' '
        flux queue stop --all &&
        flux bulksubmit -q batch --log=batch{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: $(seq 10 2 18) &&
        flux bulksubmit -q debug --log=debug{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: $(seq 10 2 18) &&
        flux queue start --all
'

test_expect_success 'job-manager: job state SSSSR / SSSSR' '
        jmgr_check_state $(cat batch1.id) S &&
        jmgr_check_state $(cat batch2.id) S &&
        jmgr_check_state $(cat batch3.id) S &&
        jmgr_check_state $(cat batch4.id) S &&
        jmgr_check_state $(cat batch5.id) R &&
        jmgr_check_state $(cat debug1.id) S &&
        jmgr_check_state $(cat debug2.id) S &&
        jmgr_check_state $(cat debug3.id) S &&
        jmgr_check_state $(cat debug4.id) S &&
        jmgr_check_state $(cat debug5.id) R
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_named1.out &&
	cat <<-EOT >counts_named1.exp &&
	batch: Job submission is enabled
	batch: Scheduling is started
	debug: Job submission is enabled
	debug: Scheduling is started
	6 alloc requests queued
	2 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_named1.exp counts_named1.out
'

test_expect_success 'job-manager: stop scheduling debug queue' '
        flux queue stop --queue=debug
'

test_expect_success 'job-manager: cancel the two running jobs' '
        flux cancel $(cat batch5.id) &&
        flux cancel $(cat debug5.id)
'

# batch queue jobs should run instead of debug queue
test_expect_success 'job-manager: job state SSRRI / SSSSI' '
        jmgr_check_state $(cat batch1.id) S &&
        jmgr_check_state $(cat batch2.id) S &&
        jmgr_check_state $(cat batch3.id) R &&
        jmgr_check_state $(cat batch4.id) R &&
        jmgr_check_state $(cat batch5.id) I &&
        jmgr_check_state $(cat debug1.id) S &&
        jmgr_check_state $(cat debug2.id) S &&
        jmgr_check_state $(cat debug3.id) S &&
        jmgr_check_state $(cat debug4.id) S &&
        jmgr_check_state $(cat debug5.id) I
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_named2.out &&
	cat <<-EOT >counts_named2.exp &&
	batch: Job submission is enabled
	batch: Scheduling is started
	debug: Job submission is enabled
	debug: Scheduling is stopped
	0 alloc requests queued
	2 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_named2.exp counts_named2.out
'

test_expect_success 'job-manager: increase urgency job 2 in debug queue' '
        flux job urgency $(cat debug2.id) 18
'

test_expect_success 'job-manager: start scheduling debug queue' '
        flux queue start --queue=debug
'

test_expect_success 'job-manager: cancel the two running jobs' '
        flux cancel $(cat batch3.id) &&
        flux cancel $(cat batch4.id)
'

# debug job 2 and 4 should have highest priority now and be running
test_expect_success 'job-manager: job state SSIII / SSRRI' '
        jmgr_check_state $(cat batch1.id) S &&
        jmgr_check_state $(cat batch2.id) S &&
        jmgr_check_state $(cat batch3.id) I &&
        jmgr_check_state $(cat batch4.id) I &&
        jmgr_check_state $(cat batch5.id) I &&
        jmgr_check_state $(cat debug1.id) S &&
        jmgr_check_state $(cat debug2.id) R &&
        jmgr_check_state $(cat debug3.id) S &&
        jmgr_check_state $(cat debug4.id) R &&
        jmgr_check_state $(cat debug5.id) I
'

test_expect_success 'job-manager: queue counts are as expected' '
	flux queue status -v >counts_named3.out &&
	cat <<-EOT >counts_named3.exp &&
	batch: Job submission is enabled
	batch: Scheduling is started
	debug: Job submission is enabled
	debug: Scheduling is started
	2 alloc requests queued
	2 alloc requests pending to scheduler
	2 running jobs
	EOT
	test_cmp counts_named3.exp counts_named3.out
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux cancel --all --states=pending &&
        flux cancel --all &&
        flux queue drain
'

test_done
