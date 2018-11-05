#!/bin/sh

test_description='Test flux job ingest service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi
if test -x ${FLUX_BUILD_DIR}/src/cmd/flux-jobspec-validate; then
    test_set_prereq ENABLE_JOBSPEC
fi
if flux job submitbench --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
    SUBMITBENCH_OPT_R="--reuse-signature"
    SUBMITBENCH_OPT_NONE="--sign-type=none"
fi

test_under_flux 4 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J=${JOBSPEC}/y2j
SUBMITBENCH="flux job submitbench $SUBMITBENCH_OPT_NONE"

DUMMY_EVENTLOG=test.ingest.eventlog

test_valid ()
{
    local rc=0
    for job in $*; do
        cat ${job} | ${Y2J} | ${SUBMITBENCH} -
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

test_expect_success 'job-ingest: submit fails without job-manager' '
	test_must_fail ${SUBMITBENCH} basic.json 2>nosys.out
'

test_expect_success 'job-ingest: load job-manager-dummy module' '
	flux module load ${FLUX_BUILD_DIR}/t/ingest/.libs/job-manager-dummy.so
'

test_expect_success 'job-ingest: can submit jobspec on stdin' '
	cat basic.json | ${SUBMITBENCH} -
'

test_expect_success 'job-ingest: YAML jobspec is rejected' '
	test_must_fail ${SUBMITBENCH} ${JOBSPEC}/valid/basic.yaml
'

test_expect_success 'job-ingest: jobspec stored accurately in KVS' '
	jobid=$(${SUBMITBENCH} basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs get --raw ${kvsdir}.jobspec >jobspec.out &&
	test_cmp basic.json jobspec.out
'

test_expect_success 'job-ingest: submitter userid stored in KVS' '
	myuserid=$(id -u) &&
	jobid=$(${SUBMITBENCH} basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	jobuserid=$(flux kvs get --json ${kvsdir}.userid) &&
	test $jobuserid -eq $myuserid
'

test_expect_success 'job-ingest: job announced to job manager' '
	jobid=$(${SUBMITBENCH} --priority=10 basic.json) &&
	flux kvs eventlog get ${DUMMY_EVENTLOG} \
		| grep "id=${jobid}" >jobman.out &&
	grep -q priority=10 jobman.out &&
	grep -q userid=$(id -u) jobman.out
'

test_expect_success 'job-ingest: priority stored in KVS' '
	jobid=$(${SUBMITBENCH} basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	jobpri=$(flux kvs get --json ${kvsdir}.priority) &&
	test $jobpri -eq 16
'

test_expect_success 'job-ingest: eventlog stored in KVS' '
	jobid=$(${SUBMITBENCH} basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog get ${kvsdir}.eventlog | grep submit
'

test_expect_success 'job-ingest: instance owner can submit priority=31' '
	jobid=$(${SUBMITBENCH} --priority=31 basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	jobpri=$(flux kvs get --json ${kvsdir}.priority) &&
	test $jobpri -eq 31
'

test_expect_success 'job-ingest: priority range is enforced' '
	test_must_fail ${SUBMITBENCH} --priority=32 basic.json &&
	test_must_fail ${SUBMITBENCH} --priority="-1" basic.json
'

test_expect_success 'job-ingest: guest cannot submit priority=17' '
	! FLUX_HANDLE_ROLEMASK=0x2 ${SUBMITBENCH} --priority=17 basic.json
'

test_expect_success 'job-ingest: valid jobspecs accepted' '
	test_valid ${JOBSPEC}/valid/*
'

test_expect_success ENABLE_JOBSPEC 'job-ingest: invalid jobs rejected' '
	test_invalid ${JOBSPEC}/invalid/*
'

test_expect_success 'job-ingest: submit job 100 times' '
	${SUBMITBENCH} -r 100 use_case_2.6.json
'

test_expect_success 'job-ingest: submit job 100 times, reuse signature' '
	${SUBMITBENCH} ${SUBMITBENCH_OPT_R} -r 100 use_case_2.6.json
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: submit user != signed user fails' '
	! FLUX_HANDLE_USERID=9999 ${SUBMITBENCH} basic.json 2>baduser.out &&
	grep -q "signer=$(id -u) != requestor=9999" baduser.out
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: non-owner mech=none fails' '
	! FLUX_HANDLE_ROLEMASK=0x2 ${SUBMITBENCH} basic.json 2>badrole.out &&
	grep -q "only instance owner" badrole.out
'

test_expect_success 'job-ingest: unload job-manager-dummy module' '
	flux module remove job-manager
'

test_done
