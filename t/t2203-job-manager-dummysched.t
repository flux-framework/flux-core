#!/bin/sh

test_description='Test flux job manager service with dummy scheduler'

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

SCHED_DUMMY=${FLUX_BUILD_DIR}/t/job-manager/.libs/sched-dummy.so

flux setattr log-stderr-level 1

get_state() {
	local id=$1
	local state=$(flux job list -s | awk '$1 == "'${id}'" { print $2; }')
	test -z "$state" \
		&& flux job wait-event --timeout=5 ${id} clean >/dev/null \
		&& state=I
	echo $state
}
check_state() {
	local id=$1
	local wantstate=$2
	for try in $(seq 1 10); do
		test $(get_state $id) = $wantstate && return 0
	done
	return 1
}

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

test_expect_success 'job-manager: load job-ingest, job-manager' '
	flux module load -r all job-ingest &&
	flux module load -r all job-info &&
	flux module load -r 0 job-manager
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

test_expect_success 'job-manager: load sched-dummy --cores=2' '
	flux module load -r 0 ${SCHED_DUMMY} --cores=2
'

test_expect_success 'job-manager: job state RRSSS' '
	check_state $(cat job1.id) R &&
	check_state $(cat job2.id) R &&
	check_state $(cat job3.id) S &&
	check_state $(cat job4.id) S &&
	check_state $(cat job5.id) S
'

test_expect_success 'job-manager: running job has alloc event' '
	flux job wait-event --timeout=5.0 $(cat job1.id) alloc
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

test_expect_success 'job-manager: first S job sent alloc, second S did not' '
	flux job wait-event --timeout=5.0 $(cat job4.id) debug.alloc-request &&
	! flux job wait-event --timeout=0.1 $(cat job5.id) debug.alloc-request
'

test_expect_success 'job-manager: canceled job has exception, free events' '
	flux job wait-event --timeout=5.0 $(cat job2.id) exception &&
	flux job wait-event --timeout=5.0 $(cat job2.id) free
'

test_expect_success 'job-manager: reload sched-dummy --cores=4' '
	flux module remove -r 0 sched-dummy &&
	flux module load -r 0 ${SCHED_DUMMY} --cores=4
'

test_expect_success 'job-manager: job state RIRRR' '
	check_state $(cat job1.id) R &&
	check_state $(cat job2.id) I &&
	check_state $(cat job3.id) R &&
	check_state $(cat job4.id) R &&
	check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel 1' '
	flux job cancel $(cat job1.id)
'

test_expect_success 'job-manager: job state IIRRR' '
	check_state $(cat job1.id) I &&
	check_state $(cat job2.id) I &&
	check_state $(cat job3.id) R &&
	check_state $(cat job4.id) R &&
	check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel all jobs' '
	flux job cancel $(cat job3.id) &&
	flux job cancel $(cat job4.id) &&
	flux job cancel $(cat job5.id)
'

test_expect_success 'job-manager: job state IIIII' '
	check_state $(cat job1.id) I &&
	check_state $(cat job2.id) I &&
	check_state $(cat job3.id) I &&
	check_state $(cat job4.id) I &&
	check_state $(cat job5.id) I
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
	flux module remove -r 0 sched-dummy
'

test_expect_success 'job-manager: remove job-manager, job-ingest' '
	flux module remove -r 0 job-manager &&
	flux module remove -r all job-info &&
	flux module remove -r all job-ingest
'

test_done
