#!/bin/sh

test_description='Test flux job manager annotate service'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
test_under_flux 1 job

flux setattr log-stderr-level 1

test_expect_success 'job-manager: initially run without scheduler' '
        flux module unload sched-simple
'
test_expect_success 'job-manager: submit 5 jobs' '
        flux submit --log=job{cc}.id --cc="1-5" --flags=debug -n1 \
           hostname
'

test_expect_success 'job-manager: no annotations (SSSSS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: no annotations in job-list (SSSSS)' '
        jlist_check_no_annotations $(cat job1.id) &&
        jlist_check_no_annotations $(cat job2.id) &&
        jlist_check_no_annotations $(cat job3.id) &&
        jlist_check_no_annotations $(cat job4.id) &&
        jlist_check_no_annotations $(cat job5.id)
'

# --setbit 0x2 enables creation of reason_pending field
# flux queue stop/start to ensure no raciness with setting up debug bits
test_expect_success 'job-manager: load sched-simple (1 rank, 2 cores/rank)' '
        flux queue stop &&
        flux module load sched-simple mode=unlimited &&
        flux module debug --setbit 0x2 sched-simple &&
        flux queue start
'

test_expect_success 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotate jobs (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: annotate jobs in job-list (RRSSS)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jlist_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jlist_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: annotate jobs in flux-jobs (RRSSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "1" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: annotate jobs (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: annotate jobs in job-list (RRSSS)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jlist_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jlist_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: annotate jobs in flux-jobs (RRSSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "1" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: cancel 2' '
        flux cancel $(cat job2.id)
'

test_expect_success 'job-manager: job state RIRSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success 'job-manager: annotate jobs (RIRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in job-list (RIRSS)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jlist_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jlist_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jlist_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jlist_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in flux-jobs (RIRSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "rank0/core1" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.reason_pending" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.jobs_ahead" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.jobs_ahead" "1"
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux cancel --all --states=pending &&
        flux cancel --all
'

test_expect_success 'job-manager: job state IIIII' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) I &&
        jmgr_check_state $(cat job4.id) I &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success 'job-manager: no annotations (IIIII)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

# compared to above, note that job ids that ran retain annotations
test_expect_success 'job-manager: annotation jobs in job-list (IIIII)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_no_annotations $(cat job4.id) &&
        jlist_check_no_annotations $(cat job5.id)
'

# compared to above, note that job ids that ran retain annotations
test_expect_success 'job-manager: annotate jobs in job-list (IIIII)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_done
