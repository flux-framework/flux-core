#!/bin/sh

test_description='Test flux job manager scheduler priority ordering (unlimited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

test_expect_success 'unload job-exec module to prevent job execution' '
        flux module remove job-exec
'

test_expect_success 'job-manager: initially run without scheduler' '
        flux module unload sched-simple
'

test_expect_success 'job-manager: submit 5 jobs (differing urgencies)' '
        flux job submit --flags=debug --urgency=12 basic.json >job1.id &&
        flux job submit --flags=debug --urgency=10 basic.json >job2.id &&
        flux job submit --flags=debug --urgency=14 basic.json >job3.id &&
        flux job submit --flags=debug --urgency=16 basic.json >job4.id &&
        flux job submit --flags=debug --urgency=18 basic.json >job5.id
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

# --setbit 0x2 enables creation of reason_pending field
# flux queue stop/start to ensure not raciness with setting up debug bits
test_expect_success 'job-manager: load sched-simple w/ 2 cores' '
        flux R encode -r0 -c0-1 >R.test &&
        flux resource reload R.test &&
        flux queue stop &&
        flux module load sched-simple unlimited &&
        flux module debug --setbit 0x2 sched-simple &&
        flux queue start
'

test_expect_success HAVE_JQ 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (RRSSS)' '
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
        flux job cancel $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: job state SSRRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (SSRRI)' '
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

test_expect_success HAVE_JQ 'job-manager: job state SSRRI' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSRRI)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job2.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job2.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel 4' '
        flux job cancel $(cat job4.id)
'

test_expect_success HAVE_JQ 'job-manager: job state SRRII' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) I &&
        jmgr_check_state $(cat job5.id) I
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSRRI)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        test_must_fail jmgr_check_annotation_exists $(cat job2.id) "sched.reason_pending" &&
        test_must_fail jmgr_check_annotation_exists $(cat job2.id) "sched.jobs_ahead" &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

# cancel non-running jobs first, to ensure they are not accidentally run when
# running jobs free resources.
test_expect_success 'job-manager: cancel all jobs' '
        flux job cancelall --states=SCHED -f &&
        flux job cancelall -f
'

test_done
