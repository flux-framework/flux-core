#!/bin/sh

test_description='Test flux job manager service with sched-simple (single)'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

export TEST_UNDER_FLUX_NO_JOB_EXEC=y
test_under_flux 2 job -Slog-stderr-level=1

# N.B. we will fake with different resources later on in this file, thus
# the need to set resources manually instead of through test_under_flux()
#
# --setbit 0x2 enables creation of reason_pending field
test_expect_success 'job-manager: load sched-simple w/ 1 rank, 2 cores/rank' '
        flux module unload sched-simple &&
        flux R encode -r0 -c0-1 >R.test &&
        flux resource reload R.test &&
        flux module load sched-simple mode=limited=1 &&
        flux module debug --setbit 0x2 sched-simple
'

test_expect_success 'job-manager: submit 5 jobs' '
        flux submit --log=job{cc}.id --cc="1-5" --flags=debug -n1 \
           hostname
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
        jmgr_check_no_annotations $(cat job4.id) &&
        jmgr_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: annotate jobs in job-list (RRSSS)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_no_annotations $(cat job4.id) &&
        jlist_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: annotate jobs in flux-jobs (RRSSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.reason_pending" "insufficient resources" &&
        fjobs_check_no_annotations $(cat job4.id) &&
        fjobs_check_no_annotations $(cat job5.id)
'

test_expect_success 'job-manager: running job has alloc event' '
        flux job wait-event --timeout=5.0 $(cat job1.id) alloc
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
        jmgr_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jmgr_check_no_annotations $(cat job5.id)
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in job-list (RIRSS)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job4.id) "sched.reason_pending" "\"insufficient resources\"" &&
        jlist_check_no_annotations $(cat job5.id)
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in flux jobs (RIRSS)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.reason_pending" "insufficient resources" &&
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

test_expect_success 'job-manager: reload sched-simple w/ 2 ranks, 2 cores/rank' '
        flux dmesg -C &&
        flux module unload sched-simple &&
        flux R encode -r0-1 -c0-1 >R2.test &&
        flux resource reload R2.test &&
        flux module load sched-simple mode=limited=1 &&
        flux module debug --setbit 0x2 sched-simple &&
        flux dmesg | grep "hello:" >hello.dmesg
'

test_expect_success 'job-manager: hello handshake found jobs 1 3' '
        grep id=$(flux job id --to=f58 < job1.id) hello.dmesg &&
        grep id=$(flux job id --to=f58 < job3.id) hello.dmesg
'

test_expect_success 'job-manager: hello handshake priority is default urgency' '
        grep priority=16 hello.dmesg
'

test_expect_success 'job-manager: hello handshake userid is expected' '
        grep userid=$(id -u) hello.dmesg
'

test_expect_success 'job-manager: job state RIRRR' '
        jmgr_check_state $(cat job1.id) R &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: annotate jobs (RIRRR)' '
        jmgr_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jmgr_check_no_annotations $(cat job2.id) &&
        jmgr_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jmgr_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank1/core0\"" &&
        jmgr_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank1/core1\""
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in job-list (RIRRR)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank1/core0\"" &&
        jlist_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank1/core1\""
'

# compared to above, note that job id #2 retains annotations, it is
# cached in job-list
test_expect_success 'job-manager: annotate jobs in flux jobs (RIRRR)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.resource_summary" "rank1/core0" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.resource_summary" "rank1/core1"
'

test_expect_success 'job-manager: cancel 1' '
        flux cancel $(cat job1.id)
'

test_expect_success 'job-manager: job state IIRRR' '
        jmgr_check_state $(cat job1.id) I &&
        jmgr_check_state $(cat job2.id) I &&
        jmgr_check_state $(cat job3.id) R &&
        jmgr_check_state $(cat job4.id) R &&
        jmgr_check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel all jobs' '
        flux cancel $(cat job3.id) &&
        flux cancel $(cat job4.id) &&
        flux cancel $(cat job5.id)
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

# compared to above, annotations are cached
test_expect_success 'job-manager: annotate jobs in job-list (IIIII)' '
        jlist_check_annotation $(cat job1.id) "sched.resource_summary" "\"rank0/core0\"" &&
        jlist_check_annotation $(cat job2.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job3.id) "sched.resource_summary" "\"rank0/core1\"" &&
        jlist_check_annotation $(cat job4.id) "sched.resource_summary" "\"rank1/core0\"" &&
        jlist_check_annotation $(cat job5.id) "sched.resource_summary" "\"rank1/core1\""
'

# compared to above, annotations are cached
test_expect_success 'job-manager: annotate jobs in flux jobs (IIIII)' '
        fjobs_check_annotation $(cat job1.id) "annotations.sched.resource_summary" "rank0/core0" &&
        fjobs_check_annotation $(cat job2.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job3.id) "annotations.sched.resource_summary" "rank0/core1" &&
        fjobs_check_annotation $(cat job4.id) "annotations.sched.resource_summary" "rank1/core0" &&
        fjobs_check_annotation $(cat job5.id) "annotations.sched.resource_summary" "rank1/core1"
'

test_expect_success 'job-manager: simulate alloc failure' '
        flux module debug --setbit 0x1 sched-simple &&
        flux submit --flags=debug -n1 hostname >job6.id &&
        flux job wait-event --timeout=5 $(cat job6.id) exception >ev6.out &&
        grep -q "type=\"alloc\"" ev6.out &&
        grep -q severity=0 ev6.out &&
        grep -q DEBUG_FAIL_ALLOC ev6.out
'

test_done
