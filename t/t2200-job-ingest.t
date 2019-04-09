#!/bin/sh

test_description='Test flux job ingest service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi
if ${FLUX_BUILD_DIR}/t/ingest/submitbench --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
    SUBMITBENCH_OPT_R="--reuse-signature"
fi

test_under_flux 4 kvs

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J=${JOBSPEC}/y2j.py
SUBMITBENCH="${FLUX_BUILD_DIR}/t/ingest/submitbench"

DUMMY_EVENTLOG=test.ingest.eventlog

test_valid ()
{
    local rc=0
    for job in $*; do
        cat ${job} | ${Y2J} | ${SUBMITBENCH} - || rc=1
    done
    return ${rc}
}

test_invalid ()
{
    local rc=0
    for job in $*; do
        cat ${job} | ${Y2J} | ${SUBMITBENCH} - && rc=1
    done
    return ${rc}
}

test_expect_success 'job-ingest: convert basic.yaml to json' '
	${Y2J} <${JOBSPEC}/valid/basic.yaml >basic.json
'

test_expect_success 'job-ingest: convert use_case_2.6.yaml to json' '
	${Y2J} <${JOBSPEC}/valid/use_case_2.6.yaml >use_case_2.6.json
'

test_expect_success 'job-ingest: submit fails without job-ingest' '
	test_must_fail flux job submit basic.json 2>nosys.out
'

test_expect_success 'job-ingest: load job-ingest && job-info' '
	flux module load -r all job-ingest &&
	flux module load -r all job-info
'

test_expect_success 'job-ingest: submit fails without job-manager' '
	test_must_fail flux job submit basic.json 2>nosys.out
'

test_expect_success 'job-ingest: load job-manager-dummy module' '
	flux module load -r 0 \
		${FLUX_BUILD_DIR}/t/ingest/.libs/job-manager-dummy.so
'

test_expect_success 'job-ingest: YAML jobspec is rejected' '
	test_must_fail flux job submit ${JOBSPEC}/valid/basic.yaml
'

test_expect_success 'job-ingest: jobspec stored accurately in KVS' '
	jobid=$(flux job submit basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs get --raw ${kvsdir}.jobspec >jobspec.out &&
	test_cmp basic.json jobspec.out
'

test_expect_success 'job-ingest: job announced to job manager' '
	jobid=$(flux job submit --priority=10 basic.json) &&
	flux kvs eventlog get ${DUMMY_EVENTLOG} \
		| grep "\"id\":${jobid}" >jobman.out &&
	grep -q "\"priority\":10" jobman.out &&
	grep -q "\"userid\":$(id -u)" jobman.out
'

test_expect_success 'job-ingest: submit event logged with userid, priority' '
	jobid=$(flux job submit --priority=11 basic.json) &&
	flux job eventlog $jobid |grep submit >eventlog.out &&
	grep -q priority=11 eventlog.out &&
	grep -q userid=$(id -u) eventlog.out
'

test_expect_success 'job-ingest: instance owner can submit priority=31' '
	flux job submit --priority=31 basic.json
'

test_expect_success 'job-ingest: priority range is enforced' '
	test_must_fail flux job submit --priority=32 basic.json &&
	test_must_fail flux job submit --priority="-1" basic.json
'

test_expect_success 'job-ingest: guest cannot submit priority=17' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux job submit --priority=17 basic.json
'

test_expect_success 'job-ingest: valid jobspecs accepted' '
	test_valid ${JOBSPEC}/valid/*
'

test_expect_success 'job-ingest: invalid jobs rejected' '
	test_invalid ${JOBSPEC}/invalid/*
'

test_expect_success 'job-ingest: submit job 100 times' '
	${SUBMITBENCH} -r 100 use_case_2.6.json
'

test_expect_success 'job-ingest: submit job 100 times, reuse signature' '
	${SUBMITBENCH} ${SUBMITBENCH_OPT_R} -r 100 use_case_2.6.json
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: submit user != signed user fails' '
	! FLUX_HANDLE_USERID=9999 flux job submit basic.json 2>baduser.out &&
	grep -q "signer=$(id -u) != requestor=9999" baduser.out
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: non-owner mech=none fails' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux job submit \
		--sign-type=none basic.json 2>badrole.out &&
	grep -q "only instance owner" badrole.out
'

test_expect_success 'job-ingest: remove modules' '
	flux module remove -r 0 job-manager &&
	flux module remove -r all job-info &&
	flux module remove -r all job-ingest
'

test_done
