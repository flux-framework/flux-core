#!/bin/sh

test_description='Test flux job manager job hold (unlimited mode)'

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

test_expect_success 'job-manager: submit 5 jobs (job 4 held)' '
        flux job submit --flags=debug basic.json >job1.id &&
        flux job submit --flags=debug basic.json >job2.id &&
        flux job submit --flags=debug basic.json >job3.id &&
        flux job submit --flags=debug --urgency=0 basic.json >job4.id &&
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

test_expect_success HAVE_JQ 'job-manager: job annotations correct (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "0" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "1"
'

test_expect_success 'job-manager: put hold on job 3' '
        flux job urgency $(cat job3.id) hold
'

test_expect_success HAVE_JQ 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: job annotations updated (RRSSS)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.jobs_ahead" "0"
'

test_expect_success 'job-manager: cancel job 1 & 2' '
        flux job cancel $(cat job1.id) &&
        flux job cancel $(cat job2.id)
'

test_expect_success HAVE_JQ 'job-manager: job state IISSR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: job annotations updated (IISSR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"1core\""
'

test_expect_success 'job-manager: remove hold on job 3' '
        flux job urgency $(cat job3.id) 16
'

test_expect_success HAVE_JQ 'job-manager: job state IISSR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: job annotations updated (IIRSR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"1core\""
'

test_expect_success 'job-manager: remove hold on job 4' '
        flux job urgency $(cat job4.id) 16
'

test_expect_success HAVE_JQ 'job-manager: job state IIRSR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: job annotations updated (IIRSR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"1core\""
'

test_expect_success 'job-manager: cancel job 3' '
        flux job cancel $(cat job3.id)
'

test_expect_success HAVE_JQ 'job-manager: job state IIIRR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) I &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: job annotations updated (IISRR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"1core\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"1core\""
'

test_expect_success 'job-manager: cancel all jobs' '
        flux job cancel $(cat job4.id) &&
        flux job cancel $(cat job5.id)
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
