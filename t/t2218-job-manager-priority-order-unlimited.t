#!/bin/sh

test_description='Test flux job manager scheduler priority ordering (unlimited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="unlimited"
test_under_flux 1 job -Slog-stderr-level=1

# N.B. resources = 1 rank, 2 cores/rank
# flux queue stop/start to ensure no scheduling until after all jobs submitted
test_expect_success 'job-manager: submit 5 jobs (differing urgencies)' '
        flux queue stop &&
        flux bulksubmit --log=job{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: 12 10 14 16 18 > jobids.out &&
        flux queue start
'

test_expect_success 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotate jobs (SSSRR)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "2" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: cancel 5' '
        flux cancel $(cat job5.id)
'

test_expect_success 'job-manager: job state SSRRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success 'job-manager: annotate jobs (SSRRI)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: increase urgency job 2' '
        flux job urgency $(cat job2.id) 20
'

test_expect_success 'job-manager: job state SSRRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success 'job-manager: annotate jobs updated (SSRRI)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs
test_expect_success 'job-manager: load priority-invert plugin' '
        flux jobtap load --remove=all ${PLUGINPATH}/priority-invert.so
'

test_expect_success 'job-manager: job state SSRRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success 'job-manager: annotate jobs updated (SSRRI)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel 4' '
        flux cancel $(cat job4.id)
'

test_expect_success 'job-manager: job state RSRII' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) I &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success 'job-manager: annotate jobs updated (RSRII)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job1.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job1.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux cancel --all --states=pending &&
        flux cancel --all
'

test_done
