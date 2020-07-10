#!/bin/sh

test_description='Test flux job manager annoate service with dummy scheduler'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

SCHED_DUMMY=${FLUX_BUILD_DIR}/t/job-manager/.libs/sched-dummy.so

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

test_expect_success 'job-manager: load job-ingest, job-manager' '
        flux module load job-manager &&
        flux module load job-ingest &&
        flux exec -r all -x 0 flux module load job-ingest &&
        flux exec -r all flux module load job-info
'

test_expect_success 'job-manager: submit 5 jobs' '
        flux job submit --flags=debug basic.json >job1.id &&
        flux job submit --flags=debug basic.json >job2.id &&
        flux job submit --flags=debug basic.json >job3.id &&
        flux job submit --flags=debug basic.json >job4.id
'

test_expect_success HAVE_JQ 'job-manager: no annotations (SSSS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id)
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 3-5 in job-info (RRSS)' '
        jinfo_check_no_annotations $(cat job1.id) &&
        jinfo_check_no_annotations $(cat job2.id) &&
        jinfo_check_no_annotations $(cat job3.id) &&
        jinfo_check_no_annotations $(cat job4.id)
'

test_expect_success HAVE_JQ 'job-manager: user annotate job id 1' '
        flux job annotate $(cat job1.id) user.key \"foo\"
'

test_expect_success HAVE_JQ 'job-manager: user annotate job id 2' '
        flux job annotate $(cat job2.id) user.key \"bar\"
'

test_expect_success HAVE_JQ 'job-manager: user annotate job id 4' '
        flux job annotate $(cat job4.id) user.key \"baz\"
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 1 & 2 (SSSS)' '
        jmgr_check_annotation $(cat job1.id) "user.key" "\"foo\"" &&
        jmgr_check_annotation $(cat job2.id) "user.key" "\"bar\"" &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "user.key" "\"baz\""
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1 & 2 in job-info (SSSS)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"foo\"" &&
        jinfo_check_annotation $(cat job2.id) "user.key" "\"bar\"" &&
        jinfo_check_no_annotations $(cat job3.id) &&
        jinfo_check_annotation $(cat job4.id) "user.key" "\"baz\""
'

test_expect_success 'job-manager: load sched-dummy --cores=2' '
        flux module load ${SCHED_DUMMY} --cores=2 --mode=unlimited
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "user.key" "\"foo\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "user.key" "\"bar\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1"
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 in job-info (RRSS)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"foo\"" &&
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job2.id) "user.key" "\"bar\"" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jinfo_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "1"
'

test_expect_success HAVE_JQ 'job-manager: user annotate job id 1 again' '
        flux job annotate $(cat job1.id) user.key \"bozo\"
'

test_expect_success HAVE_JQ 'job-manager: user clear annotatation job id 2' '
        flux job annotate $(cat job2.id) user.key null
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job2.id) "user.key" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1"
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 in job-info (RRSS)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job2.id) "user.key" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jinfo_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "1"
'

test_expect_success 'job-manager: cancel 2' '
        flux job cancel $(cat job2.id)
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 (RIRS)' '
        jmgr_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0"
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate job id 4-5 in job-info (RIRSS)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job2.id) "user.key" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jinfo_check_annotation $(cat job4.id) "user.key" "\"baz\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "0"
'

test_expect_success 'job-manager: cancel 4' '
        flux job cancel $(cat job4.id)
'

# note that job4.id loses user annotation due to cancellation of non-running job
test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 (RIRI)' '
        jmgr_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_no_annotations $(cat job4.id)
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate job id 1-4 in job-info (RIRI)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job2.id) "user.key" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jinfo_check_no_annotations $(cat job4.id)
'

test_expect_success 'job-manager: cancel all jobs' '
        flux job cancel $(cat job1.id) &&
        flux job cancel $(cat job3.id)
'

test_expect_success HAVE_JQ 'job-manager: no annotations (IIII)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id)
'

# compared to above, note that job ids that ran retain annotations
test_expect_success HAVE_JQ 'job-manager: no annotations in job-info (IIII)' '
        jinfo_check_annotation $(cat job1.id) "user.key" "\"bozo\"" &&
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job2.id) "user.key" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_no_annotations $(cat job4.id)
'

test_expect_success HAVE_JQ 'job-manager: user annotate invalid id' '
        test_must_fail flux job annotate 123456789 user.key \"foo\"
'

test_expect_success 'job-manager: remove sched-dummy' '
        flux module remove sched-dummy
'

test_expect_success 'job-manager: remove job-manager, job-ingest' '
        flux module remove job-manager &&
        flux exec -r all flux module remove job-info &&
        flux exec -r all flux module remove job-ingest
'

test_done
