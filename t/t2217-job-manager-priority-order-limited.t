#!/bin/sh

test_description='Test flux job manager scheduler priority ordering (limited)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
test_under_flux 1 job

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

# --setbit 0x2 enables creation of reason_pending field
test_expect_success 'job-manager: load sched-simple (1 rank, 2 cores/rank)' '
        flux module unload sched-simple &&
        flux module load sched-simple mode=limited=2 &&
        flux module debug --setbit 0x2 sched-simple
'

# flux queue stop/start to ensure no scheduling until after all jobs submitted
test_expect_success 'job-manager: submit 5 jobs (differing urgencies)' '
        flux queue stop &&
        flux job submit --flags=debug --urgency=10 basic.json >job1.id &&
        flux job submit --flags=debug --urgency=12 basic.json >job2.id &&
        flux job submit --flags=debug --urgency=14 basic.json >job3.id &&
        flux job submit --flags=debug --urgency=16 basic.json >job4.id &&
        flux job submit --flags=debug --urgency=18 basic.json >job5.id &&
        flux queue start
'

test_expect_success HAVE_JQ 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (SSSRR)' '
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

test_expect_success HAVE_JQ 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSSRR)' '
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

test_expect_success HAVE_JQ 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSSRR)' '
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

test_expect_success HAVE_JQ 'job-manager: job state SSSRR' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSSRR)' '
        jmgr_check_annotation $(cat job1.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job1.id) "sched.jobs_ahead" "0" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_annotation $(cat job3.id) "sched.jobs_ahead" "1" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank0/core0\""
'

test_expect_success 'job-manager: submit new job with higher urgency' '
        flux job submit --flags=debug --urgency=20 basic.json >job6.id
'

test_expect_success HAVE_JQ 'job-manager: job state SSSRRS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSSRRS)' '
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
        flux jobtap load ${PLUGINPATH}/priority-invert.so
'

test_expect_success HAVE_JQ 'job-manager: job state SSSRRS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) S &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SSSRRS)' '
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
        flux job cancel $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: job state SRSRIS' '
        jmgr_check_state $(cat job1.id) S &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) I &&
        jmgr_check_state $(cat job6.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs updated (SRSRIS)' '
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
        flux job cancelall --states=SCHED -f &&
        flux job cancelall -f
'

test_done
