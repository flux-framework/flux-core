#!/bin/sh

test_description='Test flux job manager job hold (limited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="limited=2"
test_under_flux 1 job -Slog-stderr-level=1

# N.B. resources = 1 rank, 2 cores/rank
test_expect_success 'job-manager: submit 5 jobs (job 3,4,5 held)' '
        flux bulksubmit --log=job{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: default default hold hold hold
'

test_expect_success 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: job annotations correct (RSSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: hold job 3, 5 again (issue #4940)' '
        flux job urgency $(cat job3.id) hold &&
        flux job urgency $(cat job3.id) hold &&
        flux job urgency $(cat job3.id) hold &&
        flux job urgency $(cat job3.id) hold &&
        flux job urgency $(cat job5.id) hold
'

test_expect_success 'job-manager: remove hold on job 3, 4, 5' '
        flux job urgency $(cat job3.id) 16 &&
        flux job urgency $(cat job4.id) 16 &&
        flux job urgency $(cat job5.id) 16
'

test_expect_success 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: job annotations updated (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: put hold on job 4' '
        flux job urgency $(cat job4.id) hold
'

test_expect_success 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: job annotations updated (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

test_expect_success 'job-manager: cancel job 1 & 2' '
        flux cancel $(cat job1.id) &&
        flux cancel $(cat job2.id)
'

test_expect_success 'job-manager: job state IISSR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: job annotations updated (IIRSR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core1\""
'

test_expect_success 'job-manager: remove hold on job 4' '
        flux job urgency $(cat job4.id) 16
'

test_expect_success 'job-manager: job state IISSR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: job annotations updated (IIRSR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core1\""
'

test_expect_success 'job-manager: cancel all jobs' '
        flux cancel $(cat job4.id) &&
        flux cancel $(cat job5.id)
'

test_done
