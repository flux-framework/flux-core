#!/bin/sh

test_description='Test flux job manager service with dummy scheduler'

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

SCHED_DUMMY=${FLUX_BUILD_DIR}/t/job-manager/.libs/sched-dummy.so

flux setattr log-stderr-level 1

get_state() {
	local id=$1
	flux job list -s | awk '$1 == "'${id}'" { print $2; }'
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
	flux module load -r 0 job-manager
'

test_expect_success 'job-manager: submit 5 jobs' '
	flux job submit basic.json >job1.id &&
	flux job submit basic.json >job2.id &&
	flux job submit basic.json >job3.id &&
	flux job submit basic.json >job4.id &&
	flux job submit basic.json >job5.id
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
	kvsdir=$(flux job id --to=kvs-active $(cat job1.id)) &&
	flux kvs eventlog get ${kvsdir}.eventlog | grep alloc
'

test_expect_success 'job-manager: cancel 2' '
	flux job cancel $(cat job2.id)
'

test_expect_success 'job-manager: job state RCRSS' '
	check_state $(cat job1.id) R &&
	check_state $(cat job2.id) C &&
	check_state $(cat job3.id) R &&
	check_state $(cat job4.id) S &&
	check_state $(cat job5.id) S
'

test_expect_success 'job-manager: canceled job has exception, free events' '
	kvsdir=$(flux job id --to=kvs-active $(cat job2.id)) &&
	flux kvs eventlog get ${kvsdir}.eventlog >eventlog.out &&
	grep -q exception eventlog.out &&
	grep -q free eventlog.out
'

test_expect_success 'job-manager: reload sched-dummy --cores=4' '
	flux module remove -r 0 sched-dummy &&
	flux module load -r 0 ${SCHED_DUMMY} --cores=4
'

test_expect_success 'job-manager: job state RCRRR' '
	check_state $(cat job1.id) R &&
	check_state $(cat job2.id) C &&
	check_state $(cat job3.id) R &&
	check_state $(cat job4.id) R &&
	check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel 1' '
	flux job cancel $(cat job1.id)
'

test_expect_success 'job-manager: job state CCRRR' '
	check_state $(cat job1.id) C &&
	check_state $(cat job2.id) C &&
	check_state $(cat job3.id) R &&
	check_state $(cat job4.id) R &&
	check_state $(cat job5.id) R
'

test_expect_success 'job-manager: cancel all jobs' '
	flux job cancel $(cat job3.id) &&
	flux job cancel $(cat job4.id) &&
	flux job cancel $(cat job5.id)
'

test_expect_success 'job-manager: job state CCCCC' '
	check_state $(cat job1.id) C &&
	check_state $(cat job2.id) C &&
	check_state $(cat job3.id) C &&
	check_state $(cat job4.id) C &&
	check_state $(cat job5.id) C
'

test_expect_success 'job-manager: simulate alloc failure' '
	flux module debug --setbit 0x1 sched-dummy &&
	flux job submit basic.json >job6.id &&
	check_state $(cat job6.id) C &&
	kvsdir=$(flux job id --to=kvs-active $(cat job6.id)) &&
	flux kvs eventlog get ${kvsdir}.eventlog | grep exception >ex.out &&
	grep -q type=alloc ex.out &&
	grep -q severity=0 ex.out &&
	grep -q DEBUG_FAIL_ALLOC ex.out
'

test_expect_success 'job-manager: remove sched-dummy' '
	flux module remove -r 0 sched-dummy
'

test_expect_success 'job-manager: remove job-manager, job-ingest' '
	flux module remove -r 0 job-manager &&
	flux module remove -r all job-ingest
'

test_done
