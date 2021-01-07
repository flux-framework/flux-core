#!/bin/sh

test_description='Test flux job manager service with dummy scheduler (single)'

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
        flux module load ${SCHED_DUMMY} --cores=2
'

test_expect_success HAVE_JQ 'job-manager: job state RRSSS' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) R &&
        jmgr_check_state $(cat job3.id) S &&
        jmgr_check_state $(cat job4.id) S &&
        jmgr_check_state $(cat job5.id) S
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs (RRSSS)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores available\"" &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs in job-info (RRSSS)' '
        jinfo_check_no_annotations $(cat job1.id) &&
        jinfo_check_no_annotations $(cat job2.id) &&
        jinfo_check_annotation $(cat job3.id) "sched.reason_pending" "\"no cores available\"" &&
        jinfo_check_no_annotations $(cat job4.id) &&
        jinfo_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: annotate jobs in flux-jobs (RRSSS)' '
        fjobs_check_no_annotations $(cat job1.id) &&
        fjobs_check_no_annotations $(cat job2.id) &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.reason_pending" "no cores available" &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: running job has alloc event' '
        flux job wait-event --timeout=5.0 $(cat job1.id) alloc
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
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores available\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: annotate jobs in job-info (RIRSS)' '
        jinfo_check_no_annotations $(cat job1.id) &&
        jinfo_check_no_annotations $(cat job2.id) &&
        jinfo_check_no_annotations $(cat job3.id) &&
        jinfo_check_annotation $(cat job4.id) "sched.reason_pending" "\"no cores available\"" &&
        jinfo_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: annotate jobs in flux jobs (RIRSS)' '
        fjobs_check_no_annotations $(cat job1.id) &&
        fjobs_check_no_annotations $(cat job2.id) &&
        fjobs_check_no_annotations $(cat job3.id) &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "no cores available" &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: first S job sent alloc, second S did not' '
        flux job wait-event --timeout=5.0 $(cat job4.id) debug.alloc-request &&
        ! flux job wait-event --timeout=0.1 $(cat job5.id) debug.alloc-request
'

test_expect_success 'job-manager: canceled job has exception, free events' '
        flux job wait-event --timeout=5.0 $(cat job2.id) exception &&
        flux job wait-event --timeout=5.0 $(cat job2.id) free
'

test_expect_success 'job-manager: reload sched-dummy --cores=4' '
        flux dmesg -C &&
        flux module reload ${SCHED_DUMMY} --cores=4 &&
        flux dmesg | grep "hello_cb:" >hello.dmesg
'

test_expect_success 'job-manager: hello handshake found jobs 1 3' '
        grep id=$(flux job id < job1.id) hello.dmesg &&
        grep id=$(flux job id < job3.id) hello.dmesg
'

test_expect_success 'job-manager: hello handshake priority is default urgency' '
        grep priority=16 hello.dmesg
'

test_expect_success 'job-manager: hello handshake userid is expected' '
        grep userid=$(id -u) hello.dmesg
'

test_expect_success HAVE_JQ 'job-manager: job state RIRRR' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success HAVE_JQ 'job-manager: no annotations (RIRRR)' '
        jmgr_check_no_annotations $(cat job1.id) &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_no_annotations $(cat job3.id) &&
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success HAVE_JQ 'job-manager: no annotations in job-info (RIRRR)' '
        jinfo_check_no_annotations $(cat job1.id) &&
        jinfo_check_no_annotations $(cat job2.id) &&
        jinfo_check_no_annotations $(cat job3.id) &&
        jinfo_check_no_annotations $(cat job4.id) &&
        jinfo_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: no annotations in flux jobs (RIRRR)' '
        fjobs_check_no_annotations $(cat job1.id) &&
        fjobs_check_no_annotations $(cat job2.id) &&
        fjobs_check_no_annotations $(cat job3.id) &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: cancel 1' '
        flux job cancel $(cat job1.id)
'

test_expect_success HAVE_JQ 'job-manager: job state IIRRR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel all jobs' '
        flux job cancel $(cat job3.id) &&
        flux job cancel $(cat job4.id) &&
        flux job cancel $(cat job5.id)
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

test_expect_success HAVE_JQ 'job-manager: no annotations in job-info (IIIII)' '
        jinfo_check_no_annotations $(cat job1.id) &&
        jinfo_check_no_annotations $(cat job2.id) &&
        jinfo_check_no_annotations $(cat job3.id) &&
        jinfo_check_no_annotations $(cat job4.id) &&
        jinfo_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: no annotations in flux jobs (IIIII)' '
        fjobs_check_no_annotations $(cat job1.id) &&
        fjobs_check_no_annotations $(cat job2.id) &&
        fjobs_check_no_annotations $(cat job3.id) &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: simulate alloc failure' '
        flux module debug --setbit 0x1 sched-dummy &&
        flux job submit --flags=debug basic.json >job6.id &&
        flux job wait-event --timeout=5 $(cat job6.id) exception >ev6.out &&
        grep -q "type=\"alloc\"" ev6.out &&
        grep -q severity=0 ev6.out &&
        grep -q DEBUG_FAIL_ALLOC ev6.out
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
