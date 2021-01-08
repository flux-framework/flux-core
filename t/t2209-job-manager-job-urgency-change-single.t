#!/bin/sh

test_description='Test flux job manager urgency change to job (mode=single)'

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
        flux module load job-info
'

test_expect_success 'job-manager: submit 4 jobs' '
        flux job submit --flags=debug basic.json >job1.id &&
        flux job submit --flags=debug basic.json >job2.id &&
        flux job submit --flags=debug basic.json >job3.id &&
        flux job submit --flags=debug basic.json >job4.id
'

test_expect_success HAVE_JQ 'job-manager: job state SSSS (no scheduler)' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S
'

test_expect_success HAVE_JQ 'job-manager: no annotations (SSSS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id)
'

test_expect_success 'job-manager: load sched-dummy --cores=2' '
        flux module load ${SCHED_DUMMY} --cores=2
'

test_expect_success HAVE_JQ 'job-manager: job state RRSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate job id 3 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job4.id)
'

test_expect_success HAVE_JQ 'job-manager: user annotate jobs' '
        flux job annotate $(cat job3.id) mykey foo &&
        flux job annotate $(cat job4.id) mykey bar
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 3-4 (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "user.mykey" "\"foo\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "user.mykey" "\"bar\""
'

test_expect_success 'job-manager: increase urgency of job 4' '
        flux job urgency $(cat job4.id) 20
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 3-4 updated (RRSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "user.mykey" "\"foo\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        jmgr_check_annotation $(cat job4.id) "user.mykey" "\"bar\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: cancel 2' '
        flux job cancel $(cat job2.id)
'

test_expect_success HAVE_JQ 'job-manager: job state RISR (job 4 runs instead of 3)' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 3-4 updated (RISR)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "user.mykey" "\"foo\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job4.id) "user.mykey" "\"bar\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job4.id) "sched.reason_pending"
'

test_expect_success 'job-manager: submit high urgency job' '
        flux job submit --flags=debug --urgency=20 basic.json >job5.id
'

test_expect_success HAVE_JQ 'job-manager: job state RISRS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 3-5 updated (RISRS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "user.mykey" "\"foo\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job3.id) "sched.reason_pending" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job4.id) "user.mykey" "\"bar\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"insufficient resources\""
'

test_expect_success 'job-manager: cancel 1' '
        flux job cancel $(cat job1.id)
'

test_expect_success HAVE_JQ 'job-manager: job state IISRR (job 5 runs instead of 3)' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotations in job id 3-5 updated (IISRR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "user.mykey" "\"foo\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job4.id) "user.mykey" "\"bar\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"1core\""
'

test_expect_success 'job-manager: cancel all jobs' '
        flux job cancel $(cat job3.id) &&
        flux job cancel $(cat job4.id) &&
        flux job cancel $(cat job5.id)
'

test_expect_success 'job-manager: remove sched-dummy' '
        flux module remove sched-dummy
'

test_expect_success 'job-manager: remove job modules' '
        flux module remove job-info &&
        flux module remove job-manager &&
        flux exec -r all flux module remove job-ingest
'

test_done
