#!/bin/sh

test_description='Test flux job manager service with dummy scheduler (unlimited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

SCHED_DUMMY=${FLUX_BUILD_DIR}/t/job-manager/.libs/sched-dummy.so

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

test_expect_success 'job-manager: load job modules' '
        flux module load job-manager &&
        flux module load job-ingest &&
        flux exec -r all -x 0 flux module load job-ingest &&
        flux exec -r all flux module load job-info
'

test_expect_success 'job-manager: submit 5 jobs' '
        flux job submit --flags=debug basic.json >job1.id &&
        flux job submit --flags=debug basic.json >job2.id &&
        flux job submit --flags=debug basic.json >job3.id &&
        flux job submit --flags=debug basic.json >job4.id &&
        flux job submit --flags=debug basic.json >job5.id
'

test_expect_success HAVE_JQ 'job-manager: job state SSSSS (no scheduler)' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: no annotations (SSSSS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: load sched-dummy --cores=2' '
        flux module load ${SCHED_DUMMY} --cores=2 --mode=unlimited
'

test_expect_success HAVE_JQ 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs job-info (RRSSS)' '
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        jinfo_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs in flux-jobs (RRSSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "1" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: cancel 2' '
        flux job cancel $(cat job2.id)
'

test_expect_success HAVE_JQ 'job-manager: job state RIRSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (RIRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate jobs in job-info (RIRSS)' '
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jinfo_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate jobs in flux-jobs (RIRSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "1core" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.reason_pending" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.jobs_ahead" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.jobs_ahead" "1"
'

test_expect_success 'job-manager: cancel 5' '
        flux job cancel $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: job state RIRSI' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (RIRSI)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jmgr_check_no_annotations $(cat job5.id)
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate jobs in job-info (RIRSS)' '
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail jinfo_check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jinfo_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jinfo_check_no_annotations $(cat job5.id)
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-info
test_expect_success HAVE_JQ 'job-manager: annotate jobs in flux jobs (RIRSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "1core" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.reason_pending" &&
        test_must_fail fjobs_check_annotation_exists $(cat job3.id) "annotations.sched.jobs_ahead" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.jobs_ahead" "0" &&
        fjobs_check_no_annotations $(cat job5.id)
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux job cancelall --states=SCHED -f &&
        flux job cancelall -f
'

test_expect_success HAVE_JQ 'job-manager: job state IIIII' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) I &&
        jmgr_check_state $(cat job4.id) I &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: no annotations (IIIII)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

# compared to above, note that job ids that ran retain annotations
test_expect_success HAVE_JQ 'job-manager: no annotations in canceled jobs in job-info (IIIII)' '
        jinfo_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        jinfo_check_no_annotations $(cat job4.id) &&
        jinfo_check_no_annotations $(cat job5.id)
'

# compared to above, note that job ids that ran retain annotations
test_expect_success HAVE_JQ 'job-manager: no annotations in canceled jobs in flux jobs (IIIII)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "1core" &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: remove sched-dummy' '
        flux module remove sched-dummy
'

test_expect_success 'job-manager: remove job modules' '
        flux exec -r all flux module remove job-info &&
        flux module remove job-manager &&
        flux exec -r all flux module remove job-ingest
'

test_done
