#!/bin/sh

test_description='Test flux job manager scheduler priority ordering (limited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
export TEST_UNDER_FLUX_SCHED_SIMPLE_MODE="limited=2"
test_under_flux 1 job -Slog-stderr-level=1

# N.B. resources = 1 rank, 2 cores/rank
# flux queue stop/start to ensure no scheduling until after all jobs submitted
test_expect_success 'job-manager: submit 5 jobs (differing urgencies)' '
        flux queue stop &&
        flux bulksubmit --log=job{seq1}.id --urgency={} --flags=debug -n1 \
           hostname ::: $(seq 10 2 18) &&
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
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: increase urgency job 2' '
        flux job urgency $(cat job2.id) 16
'

test_expect_success 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotate jobs updated (SSSRR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: increase urgency job 1' '
        flux job urgency $(cat job1.id) 18
'

test_expect_success 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotate jobs updated (SSSRR)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "1" &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: decrease urgency job 2' '
        flux job urgency $(cat job2.id) 8
'

test_expect_success 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotate jobs updated (SSSRR)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: submit new job with higher urgency' '
        flux submit --flags=debug --urgency=20 -n1 hostname >job6.id
'

test_expect_success 'job-manager: job state SSSRRS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success 'job-manager: annotate jobs updated (SSSRRS)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "1" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job6.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job6.id) "sched.jobs_ahead" "0"
'

# Notes:
#
# job urgencies at this point
# job1 - 18
# job2 - 8
# job3 - 14
# job6 - 20
#
# so inversion priority plugin will change to
#
# job1 - 13
# job2 - 23
# job3 - 17
# job6 - 11
#
# making job 2 next in line to run, job 3 after it

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs
test_expect_success 'job-manager: load priority-invert plugin' '
        flux jobtap load --remove=all ${PLUGINPATH}/priority-invert.so
'

test_expect_success 'job-manager: job state SSSRRS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success 'job-manager: annotate jobs updated (SSSRRS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job6.id)
'

test_expect_success 'job-manager: cancel 5' '
        flux cancel $(cat job5.id)
'

test_expect_success 'job-manager: job state SRSRIS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success 'job-manager: annotate jobs updated (SRSRIS)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core0\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job2.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job2.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id) &&
        jmgr_check_no_annotations $(cat job6.id)
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux cancel --all --states=pending &&
        flux cancel --all
'

test_done
