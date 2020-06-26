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
        flux job submit --flags=debug basic.json >job4.id &&
        flux job submit --flags=debug basic.json >job5.id
'

test_expect_success 'job-manager: job state SSSSS (no scheduler)' '
        check_state $(cat job1.id) S &&
        check_state $(cat job2.id) S &&
        check_state $(cat job3.id) S &&
        check_state $(cat job4.id) S &&
        check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: no annotations (SSSSS)' '
        check_no_annotations $(cat job1.id) &&
        check_no_annotations $(cat job2.id) &&
        check_no_annotations $(cat job3.id) &&
        check_no_annotations $(cat job4.id) &&
        check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: load sched-dummy --cores=2' '
        flux module load ${SCHED_DUMMY} --cores=2 --mode=unlimited
'

test_expect_success 'job-manager: job state RRSSS' '
        check_state $(cat job1.id) R &&
        check_state $(cat job2.id) R &&
        check_state $(cat job3.id) S &&
        check_state $(cat job4.id) S &&
        check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 3-5 (RRSSS)' '
        check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job4.id) "sched.jobs_ahead" "1" &&
        check_annotation $(cat job5.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job5.id) "sched.jobs_ahead" "2"
'

test_expect_success 'job-manager: cancel 2' '
        flux job cancel $(cat job2.id)
'

test_expect_success 'job-manager: job state RIRSS' '
        check_state $(cat job1.id) R &&
        check_state $(cat job2.id) I &&
        check_state $(cat job3.id) R &&
        check_state $(cat job4.id) S &&
        check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 4-5 (RIRSS)' '
        check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        check_no_annotations $(cat job2.id) &&
        check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        check_annotation $(cat job5.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

test_expect_success 'job-manager: cancel 5' '
        flux job cancel $(cat job5.id)
'

test_expect_success 'job-manager: job state RIRSI' '
        check_state $(cat job1.id) R &&
        check_state $(cat job2.id) I &&
        check_state $(cat job3.id) R &&
        check_state $(cat job4.id) S &&
        check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 4 (RIRSI)' '
        check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        check_no_annotations $(cat job2.id) &&
        check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        test_must_fail check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        test_must_fail check_annotation_exists $(cat job3.id) "sched.jobs_ahead" &&
        check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel all jobs' '
        flux job cancel $(cat job1.id) &&
        flux job cancel $(cat job3.id) &&
        flux job cancel $(cat job4.id)
'

test_expect_success 'job-manager: job state IIIII' '
        check_state $(cat job1.id) I &&
        check_state $(cat job2.id) I &&
        check_state $(cat job3.id) I &&
        check_state $(cat job4.id) I &&
        check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: no annotations (IIIII)' '
        check_no_annotations $(cat job1.id) &&
        check_no_annotations $(cat job2.id) &&
        check_no_annotations $(cat job3.id) &&
        check_no_annotations $(cat job4.id) &&
        check_no_annotations $(cat job5.id)
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
