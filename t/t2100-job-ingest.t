#!/bin/sh

test_description='Test flux job ingest service'

. $(dirname $0)/sharness.sh

if ${FLUX_BUILD_DIR}/t/ingest/submitbench --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
    SUBMITBENCH_OPT_R="--reuse-signature"
fi

test_under_flux 4 kvs

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"
SUBMITBENCH="${FLUX_BUILD_DIR}/t/ingest/submitbench"
RPC=${FLUX_BUILD_DIR}/t/request/rpc

DUMMY_EVENTLOG=test.ingest.eventlog

DUMMY_MAX_JOBID=16777216000000
DUMMY_FLUID_TS=1000000

# load|reload ingest modules (in proper order) with specified arguments
ingest_module ()
{
    cmd=$1; shift
    flux module ${cmd} job-ingest $* &&
    flux exec -r all -x 0 flux module ${cmd} job-ingest $*
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

test_expect_success 'job-ingest: load job-manager-dummy module' '
	flux module load \
		${FLUX_BUILD_DIR}/t/ingest/.libs/job-manager.so
'

test_expect_success 'job-ingest: job-ingest fails with bad option' '
	test_must_fail flux module load job-ingest badopt=xyz
'

test_expect_success 'job-ingest: load job-ingest: require-version=any' '
	ingest_module load \
		validator-plugins=jobspec \
		validator-args=--require-version=any
'

test_expect_success 'job-ingest: dummy job-manager has expected max_jobid' '
	max_jobid=$(${RPC} job-manager.getinfo | jq .max_jobid) &&
	test ${max_jobid} -eq ${DUMMY_MAX_JOBID}
'

test_expect_success 'job-ingest: max_jobid <= rank 0 FLUID timestamp' '
	ts0=$(${RPC} job-ingest.getinfo | jq .timestamp) &&
	test ${DUMMY_FLUID_TS} -le ${ts0}
'

test_expect_success 'job-ingest: rank 0 FLUID timestamp <= rank 1' '
	ts1=$(flux exec -r1 ${RPC} job-ingest.getinfo | jq .timestamp) &&
	test ${ts0} -le ${ts1}
'

test_expect_success 'job-ingest: YAML jobspec is rejected' '
	test_must_fail flux job submit ${JOBSPEC}/valid/basic.yaml
'

test_expect_success 'job-ingest: fetch jobspec from KVS' '
	jobid=$(flux job submit basic.json) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs get --raw ${kvsdir}.jobspec >jobspec.out
'
test_expect_success 'job-ingest: jobspec stored accurately in KVS' '
	jq --sort-keys . <basic.json >basic.json.normalized &&
	jq --sort-keys . <jobspec.out >jobspec.out.normalized &&
	test_cmp basic.json.normalized jobspec.out.normalized
'

test_expect_success 'job-ingest: submit a job with environment' '
	flux run --env=-* --env=FOO=bar --dry-run /bin/true \
	    >jobspec_env.json &&
	jobid=$(flux job submit jobspec_env.json) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs get --raw ${kvsdir}.jobspec >jobspec_env.out
'
test_expect_success 'job-ingest: KVS jobspec lacks environment' '
	jq -e ".attributes.system.environment.FOO == \"bar\"" \
	    <jobspec_env.json &&
	test_must_fail jq -e ".attributes.system.environment.FOO == \"bar\"" \
	    <jobspec_env.out
'

test_expect_success 'job-ingest: job announced to job manager' '
	jobid=$(flux job submit --urgency=10 basic.json | flux job id) &&
	flux kvs eventlog get ${DUMMY_EVENTLOG} \
		| grep "id=${jobid}" >jobman.out &&
	grep -q "urgency=10" jobman.out &&
	grep -q "userid=$(id -u)" jobman.out
'

test_expect_success 'job-ingest: instance owner can submit urgency=31' '
	flux job submit --urgency=31 basic.json
'

test_expect_success 'job-ingest: urgency range is enforced' '
	test_must_fail flux job submit --urgency=32 basic.json &&
	test_must_fail flux job submit --urgency="-1" basic.json
'

test_expect_success 'job-ingest: guest cannot submit urgency=17' '
	test_must_fail bash -c "FLUX_HANDLE_ROLEMASK=0x2 \
		flux job submit --urgency=17 basic.json"
'

test_expect_success 'job-ingest: guest cannot submit --flags=novalidate' '
        test_must_fail bash -c "FLUX_HANDLE_ROLEMASK=0x2 \
                flux job submit --flags=novalidate basic.json"
'

test_expect_success NO_ASAN 'job-ingest: submit job 100 times' '
	${SUBMITBENCH} -r 100 use_case_2.6.json
'

test_expect_success NO_ASAN 'job-ingest: submit job 100 times, reuse signature' '
	${SUBMITBENCH} ${SUBMITBENCH_OPT_R} -r 100 use_case_2.6.json
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: submit user != signed user fails' '
	test_must_fail bash -c "FLUX_HANDLE_USERID=9999 \
		flux job submit basic.json" 2>baduser.out &&
	grep -q "signer=$(id -u) != requestor=9999" baduser.out
'

test_expect_success HAVE_FLUX_SECURITY 'job-ingest: non-owner mech=none fails' '
	test_must_fail bash -c "FLUX_HANDLE_ROLEMASK=0x2 flux job submit \
		--sign-type=none basic.json" 2>badrole.out &&
	grep -q "only instance owner" badrole.out
'

test_expect_success 'submit request with empty payload fails with EPROTO(71)' '
	${RPC} job-ingest.submit 71 </dev/null
'

test_expect_success 'job-ingest: reload dummy job-manager in fail mode' '
	ingest_module reload batch-count=4 &&
	flux module remove job-manager &&
	flux module load \
	    ${FLUX_BUILD_DIR}/t/ingest/.libs/job-manager.so force_fail
'

test_expect_success 'job-ingest: handle total batch failure in job-ingest' '
	test_must_fail flux submit --cc=1-4 hostname
'

test_expect_success 'flux module stats job-ingest is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats job-ingest >/dev/null
'

test_expect_success 'reload the real job-manager' '
	flux module reload job-manager
'
test_expect_success 'unload job-ingest on all ranks' '
	flux exec -r all flux module remove job-ingest
'
test_expect_success 'load job-ingest max-fluid-generator-id=1000000 fails' '
	test_must_fail flux module load job-ingest \
	    max-fluid-generator-id=1000000
'
test_expect_success 'reload job-ingest max-fluid-generator-id=2 on all ranks' '
	for rank in $(seq 0 3); do \
	    flux exec -r $rank flux module load job-ingest \
	        max-fluid-generator-id=2; \
	done
'
test_expect_success 'module is not loaded on rank 3' '
	flux exec -r 3 flux module list >list3.out &&
	test_must_fail grep job-ingest list3.out
'
test_expect_success 'but a job can be submitted on rank 3' '
	flux exec -r 3 flux submit true
'

test_expect_success 'job-ingest: remove modules' '
	flux module remove -f job-manager &&
	flux exec -r all flux module remove -f job-ingest
'

test_done
