#!/bin/sh

test_description='Test flux job manager job hold (single mode)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_CORES_PER_RANK=1
export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="limited=1"
test_under_flux 1 job -Slog-stderr-level=1

# N.B. resources = 1 rank, 1 core/rank
test_expect_success 'job-manager: submit 5 jobs (job 2 held)' '
        flux bulksubmit --log=job{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: default hold default default default
'

test_expect_success 'job-manager: job state RSSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations job 3 pending, job 2 held (RSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: job 4 hold' '
        flux job urgency $(cat job4.id) 0
'

test_expect_success 'job-manager: cancel job 1' '
        flux cancel $(cat job1.id)
'

test_expect_success 'job-manager: job state ISRSS (job 3 run, job 2 held)' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations job 5 pending (job 2/4 held)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: job 2, 4 hold again (issue #4940)' '
        flux job urgency $(cat job2.id) hold &&
        flux job urgency $(cat job4.id) hold &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job4.id) S
'

test_expect_success 'job-manager: job 4 release (higher urgency)' '
        flux job urgency $(cat job4.id) 20
'

test_expect_success 'job-manager: annotations job 4 pending' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel job 3' '
        flux cancel $(cat job3.id)
'

test_expect_success 'job-manager: job state ISIRS (job 4 run, job 2 held)' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) I &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations job 5 pending (job 2 held)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: cancel job 4' '
        flux cancel $(cat job4.id)
'

test_expect_success 'job-manager: job state ISIIR (job 5 run, job 2 held)' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) I &&
        jmgr_check_state $(cat job4.id) I &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotations no job pending (job 2 held)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: job 2 release' '
        flux job urgency $(cat job2.id) 16
'

test_expect_success 'job-manager: annotations job 2 pending' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: cancel remaining jobs' '
        flux cancel $(cat job2.id) &&
        flux cancel $(cat job5.id)
'

test_done
