#!/bin/sh

test_description='Test flux job ingest service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi
if test -x ${FLUX_BUILD_DIR}/src/cmd/flux-jobspec-validate; then
    test_set_prereq ENABLE_JOBSPEC
fi
if flux job --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
    SUBMITBENCH_OPT_R="--reuse-signature"
    SUBMITBENCH_OPT_NONE="--sign-type=none"
fi

test_under_flux 4 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
SUBMITBENCH="flux job submitbench $SUBMITBENCH_OPT_NONE"

test_valid ()
{
    local rc=0
    for job in $*; do
        ${SUBMITBENCH} ${job} || rc=1
    done
    return ${rc}
}

test_invalid ()
{
    local rc=0
    for job in $*; do
        ${SUBMITBENCH} ${job} && rc=1
    done
    return ${rc}
}

test_expect_success 'job-ingest: can submit jobspec on stdin' '
	cat ${JOBSPEC}/valid/basic.yaml | ${SUBMITBENCH} -
'

test_expect_success 'job-ingest: jobspec stored accurately in KVS' '
	jobid=$(${SUBMITBENCH} ${JOBSPEC}/valid/basic.yaml) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs get --raw ${kvsdir}.jobspec >jobspec.out &&
	test_cmp ${JOBSPEC}/valid/basic.yaml jobspec.out
'

test_expect_success 'job-ingest: submitter userid stored in KVS' '
	myuserid=$(id -u) &&
	jobid=$(${SUBMITBENCH} ${JOBSPEC}/valid/basic.yaml) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	jobuserid=$(flux kvs get --json ${kvsdir}.userid) &&
	test $jobuserid -eq $myuserid
'

test_expect_success 'job-ingest: valid jobspecs accepted' '
	test_valid ${JOBSPEC}/valid/*
'

test_expect_success ENABLE_JOBSPEC 'job-ingest: invalid jobs rejected' '
	test_invalid ${JOBSPEC}/invalid/*
'

test_expect_success 'job-ingest: submit job 100 times' '
	${SUBMITBENCH} -r 100 ${JOBSPEC}/valid/use_case_2.6.yaml
'

test_expect_success 'job-ingest: submit job 100 times, reuse signature' '
	echo $SUBMITBENCH_OPT_R &&
	${SUBMITBENCH} ${SUBMITBENCH_OPT_R} \
		-r 100 ${JOBSPEC}/valid/use_case_2.6.yaml
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: submit user != signed user fails' '
	! FLUX_HANDLE_USERID=9999 ${SUBMITBENCH} \
	     ${JOBSPEC}/valid/basic.yaml 2>baduser.out &&
	grep -q permitted baduser.out
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: non-owner mech=none fails' '
	! FLUX_HANDLE_ROLEMASK=0x2 ${SUBMITBENCH} \
	     ${JOBSPEC}/valid/basic.yaml 2>badrole.out &&
	grep -q permitted badrole.out
'

test_done
