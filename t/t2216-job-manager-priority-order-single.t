#!/bin/sh

test_description='Test flux job manager urgency change to job (mode=single)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="limited=1"
test_under_flux 1 job -Slog-stderr-level=1

# N.B. resources = 1 rank, 2 cores/rank
test_expect_success 'job-manager: submit 4 jobs' '
        flux submit --log=job{cc}.id --cc="1-4" --flags=debug -n1 \
           hostname
'

test_expect_success 'job-manager: job state RRSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S
'

test_expect_success 'job-manager: annotate job id 3 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job4.id)
'

test_expect_success 'job-manager: annotations in job id 3-4 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: increase urgency of job 4' '
        flux job urgency $(cat job4.id) 20
'

test_expect_success 'job-manager: annotations in job id 3-4 updated (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: cancel 2' '
        flux cancel $(cat job2.id)
'

test_expect_success 'job-manager: job state RISR (job 4 runs instead of 3)' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R
'

test_expect_success 'job-manager: annotations in job id 3-4 updated (RISR)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job4.id) "sched.reason_pending"
'

test_expect_success 'job-manager: submit high urgency job' '
        flux submit --flags=debug --urgency=20 -n1 hostname >job5.id
'

test_expect_success 'job-manager: job state RISRS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations in job id 3-5 updated (RISRS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\""
'

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs
test_expect_success 'job-manager: load priority-invert plugin' '
        flux jobtap load --remove=all ${PLUGINPATH}/priority-invert.so
'

test_expect_success 'job-manager: job state RISRS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations in job id 3-5 updated (RISRS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel 1' '
        flux cancel $(cat job1.id)
'

test_expect_success 'job-manager: job state IISRR (job 5 runs instead of 3)' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotations in job id 3-5 updated (IISRR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: cancel all jobs' '
        flux cancel $(cat job5.id) &&
        flux cancel $(cat job4.id) &&
        flux cancel $(cat job3.id)
'

test_done
